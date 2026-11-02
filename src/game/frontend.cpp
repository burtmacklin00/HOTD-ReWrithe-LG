// The PC port's front end (frontend.h). Ported from THOTD.EXE (Watcom register calls: eax, edx, ebx, ecx, then the
// stack; the Ghidra C in private\decomp\pc\thotd.c mixes those up, so the arguments here follow tools/x86dis.py).
// UNVERIFIED (teaser): compared by eye with a PC harness capture of the main menu only.
#include "game/frontend.h"
#include "game/pad_prompts.h"
#include "game/pcmode_scores.h"

#include "game/bossmode.h"

#include "game/launcher.h"
#include "game/lang.h"
#include "game/chapters.h"
#include "game/menu_pages.h"
#include "game/pads.h"
#include "game/player_colours.h"
#include "game/scene_view.h"
#include "game/pm_portraits.h"
#include "util/png.h"
#include "util/utf8.h"

#include <algorithm>
#include <functional>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

#include "hotd_version.h"   // generated (CMakeLists.txt)
#include "util/update_check.h"

namespace hotd::game {

std::vector<std::string> sceneRowLabels();   // game/scene_view.cpp: the Cut-scenes page's rows

// rtl (a right-to-left language, lang::rtl): a text's alignment in its box mirrored: a left-aligned one to the right,
// a right-aligned one to the left (a row's label right, its value left); centred ones stay
static uint32_t rtlAlign(uint32_t flags)
{
    return lang::rtl() && !(flags & 0x20) ? flags ^ 0x04 : flags;
}

// ---- the string table 0x5CCD64 (100 records {s16 id, char* per language}; English, the language 0x5CE8BC = 0) ----
const char* Frontend::label(int id)
{
    static const char* const kEnglish[100] = {
        "Main Menu", "Arcade", "PC Mode", "Kids", "Profiles", "Ranking", "Options", "Quit", "Are you sure?", "Yes",
        "No", "Options", "Difficulty", "Life", "Continues", "Sight Speed", "Sight Graphic", "Auto Reload",
        "Blood Color", "Sound FX", "Controller Config", "Sound Effects", "Music", "Very Easy", "Easy", "Normal",
        "Hard", "Very Hard", "Green", "Red", "Yellow", "Blue", "Purple", "None", "Stereo", "Mono", "Exit", "On",
        "Off", "Mouse", "Keyboard 1", "Keyboard 2", "Joystick 1", "Joystick 2", "Lightgun 1", "Lightgun 2", "Shoot",
        "Reload", "Up", "Down", "Left", "Right", "1P", "2P", "Button 1", "Button 2", "Button 3", "Button 4",
        "Button 5", "Button 6", "Button 7", "Button 8", " ", "-", "Next", "Last", "Boss Mode", "High Scores",
        "Arcade Mode", "Chariot Boss", "Hangedman Boss", "Hermit Boss", "Magician Boss", "All Bosses",
        "Congratulations!", "You have a high score.", "Please enter your name:", "Player 1", "Player 2", "Start",
        "No Player", "Hit Points:", "Chamber:", "Bullet Damage:", "Reload Time:", "Hit Size:", "Short", "Long", "Low",
        "Medium", "High", "Small", "Large", "Resolution", "Continue", "PAUSED", "Calibrate Gun", "Gun Calibration",
        "First shot calibrates", "Start button restarts"};
    // the table is read from THOTD.EXE's data by id (record +0 = the id); ids 0..0x63 are in order there
    return id >= 0 && id < 100 ? kEnglish[id] : "";
}

// ---- 0x428494: the SPR.RBH sprite of character c in the font the flags pick ----
int Frontend::glyph(uint8_t c, uint32_t flags)
{
    // 0x1000000 / 0x2000000: the tables 0x5CF4A2 / 0x5CF6A2 / 0x5CF2A2 (not used by the main menu; not ported)
    if (flags & 0x3000000)
        return 0;
    int i;
    if (c >= 0x20 && c <= 0x7E)
        i = c - 0x20;
    else if (c >= 0xBF && c != 0xFF)
        i = c - 0xBF;   // as the original (its accented glyphs follow '~' in each font)
    else
        return 0;
    if ((flags & 0x20000) && (flags & 0x80000))
        return i + 2;       // grey 24 (0x03 = '!')
    if ((flags & 0x20000) && (flags & (0x40000 | kTextYellow)))
        return i + 0x23C;   // yellow 24 (hovered; ours: kTextYellow, the chosen item)
    if (flags & 0x20000)
        return i + 0x19D;   // white 24
    if (flags & 0x10000)
        return i + 0xFE;    // white 32
    if (flags & 0x400000)
        return i + 0x2DB;   // 11 x 21 digital
    if (flags & 0x800000)
        return i + 0x37A;   // 6 x 10
    return 0;
}

// ---- widgets ----
Widget* Frontend::widgetNew()
{
    pool_.push_back(std::make_unique<Widget>());
    return pool_.back().get();
}

void Frontend::addChild(Widget* parent, Widget* child)
{
    if (parent->child)
        child->next = parent->child;
    parent->child = child;
}

void Frontend::freeTree()
{
    pool_.clear();
    rPreviewRow_.clear();   // (fix28: the comparison pictures' widgets were this tree's)
    root_ = nullptr;
    cw_ = kWidth;   // remakemenu: the PC's 640 x 480 (remakeBegin widens a Remake screen after this)
    sc_ = 1;
    rebuild_ = nullptr;
    rScroll_ = nullptr;
    rTabCur_ = -1;
    jbTime_ = jbBar_ = nullptr;   // (menuperf: the Jukebox's live widgets went with the tree)
    jbRows_.clear();
    slbars_.clear();
    sbars_.clear();   // (menufb: the scroll bars and the range's widgets went with it; a page built again sets them)
    rangeOn_ = rangeHand_ = false;
    rangeBg_ = nullptr;
    for (Widget*& w : rangeTw_) w = nullptr;
    for (Widget*& w : rangeXw_) w = nullptr;
}

// remakemenu: a sprite at (x, y) menu units: the PC's draw on a 640 x 480 screen, scaled by sc_ on a Remake screen
void Frontend::blit(int id, int x, int y)
{
    if (sc_ == 1) {
        spr_.draw(*canvas_, id, x, y, 0, 0, cw_, kHeight, cw_);
        return;
    }
    const std::vector<uint8_t> img = spr_.image(id);
    const int iw = spr_.width(id), ih = spr_.height(id);
    if (img.size() < size_t(iw) * size_t(ih) * 4)
        return;
    const int pw = rpw(), ph = rph();   // (blood2: ps_ pixels a unit, nearest)
    const int x0 = int(std::lround(float(x) * ps_)), y0 = int(std::lround(float(y) * ps_));
    for (int j = 0; j < int(float(ih) * ps_); ++j) {
        const int dy = y0 + j;
        if (dy < 0 || dy >= ph)
            continue;
        for (int i = 0; i < int(float(iw) * ps_); ++i) {
            const int dx = x0 + i;
            const uint8_t* s = &img[(size_t(std::min(ih - 1, int(float(j) / ps_))) * size_t(iw) + size_t(std::min(iw - 1, int(float(i) / ps_)))) * 4];
            if (dx >= 0 && dx < pw && s[3])
                std::memcpy(&(*canvas_)[(size_t(dy) * size_t(pw) + size_t(dx)) * 4], s, 4);
        }
    }
}

void Frontend::setRect(Widget* w, int x, int y, int ww, int hh)
{
    w->x = x, w->y = y, w->w = ww, w->h = hh;
}

void Frontend::setLabel(Widget* w, int id, uint32_t flags)
{
    w->id = int16_t(id);
    w->text.clear();
    w->flags = (w->flags & ~0xFFFFu) | (w->flags & 0xDF01u);
    w->draw = &Frontend::drawText;
    w->flags |= flags | 0x1100;
}

void Frontend::setText(Widget* w, const std::string& s, uint32_t flags)
{
    w->text = s;
    w->draw = &Frontend::drawText;
    w->flags = (w->flags & ~0xFFFFu) | (w->flags & 0xEF01u);
    w->id = -1;
    w->flags |= flags | 0x2100;
}

void Frontend::setImage(Widget* w, int id, int idHover, uint32_t flags)
{
    w->id = int16_t(id);
    w->draw = &Frontend::drawImage;
    w->flags = (w->flags & ~0xFFFFu) | (w->flags & 0xCE81u);
    w->idHover = int16_t(idHover);
    w->flags |= flags | 0x1080;
}

void Frontend::setHandlers(Widget* w, std::function<void(Widget&)> act, bool (Frontend::*hit)(Widget&, int, int))
{
    w->activate = std::move(act);
    w->hit = hit;
    w->flags |= 0x800;
}

// ---- the rect stack ----
void Frontend::stackReset()
{
    stack_.clear();
    stack_.push_back({nullptr, 0, 0, cw_, kHeight});
}

void Frontend::push(const Widget& w)
{
    const Rect& p = stack_.back();
    Rect r{&w, p.x + w.x, p.y + w.y, w.w ? w.w : p.ww - w.x, w.h ? w.h : p.hh - w.y};
    stack_.push_back(r);
}

void Frontend::absRect(const Widget& w, int& x, int& y, int& ww, int& hh) const
{
    const Rect& p = stack_.back();
    x = p.x + w.x;
    y = p.y + w.y;
    ww = w.w ? w.w : p.ww;
    hh = w.h ? w.h : p.hh;
}

// ---- 0x427998: marks the widgets under the pointer whose hit test passes as hovered (flags 0x40000) ----
void Frontend::hoverWalk(Widget* w)
{
    for (; w && walking_; w = w->next) {
        int x, y, ww, hh;
        absRect(*w, x, y, ww, hh);
        if (x <= px_ && px_ < x + ww && y <= py_ && py_ < y + hh) {
            if (w->activate && w->hit && (this->*w->hit)(*w, px_, py_))
                w->flags |= 0x40000;
            if (!w->help.empty())
                help_ = w->help;   // ours: the innermost widget with a help line under the pointer
            if (w->child) {
                push(*w);
                hoverWalk(w->child);
            }
        }
    }
    if (stack_.size() > 1)
        stack_.pop_back();
}

void Frontend::drawW(Widget& w)
{
    if (w.raw) {
        const RawText raw(this);
        (this->*w.draw)(w);
    } else
        (this->*w.draw)(w);
}

// ---- 0x427AE8: draws every widget (a blinking one, 0x4000, only in the blink's on half), clears the hover mark ----
void Frontend::drawWalk(Widget* w)
{
    for (; w && walking_; w = w->next) {
        if (w->draw && !w->live && !w->over && (!(w->flags & 0x4000) || blink_))   // (menuperf / optui: a live / over widget: its own part)
            drawW(*w);
        if (!walking_)
            break;
        if ((w->flags & 0x40000) && w->litKids)   // ours: a hovered settings row lights its value too
            for (Widget* c = w->child; c; c = c->next)
                c->flags |= 0x40000;
        w->flags &= ~0x40000u;
        if (w->child) {
            push(*w);
            drawWalk(w->child);
        }
    }
    if (stack_.size() > 1)
        stack_.pop_back();
}

// ---- 0x427810: runs the activate handler of the widget under the pointer (the first walk that does ends it) ----
void Frontend::activateWalk(Widget* w)
{
    for (; w && walking_; w = w->next) {
        int x, y, ww, hh;
        absRect(*w, x, y, ww, hh);
        if (x <= px_ && px_ < x + ww && y <= py_ && py_ < y + hh) {
            // the original also requires 0x5CED94 == 1, or == 2 with flags 0x8000 (held-button widgets)
            if (w->activate && w->hit && (this->*w->hit)(*w, px_, py_)) {
                Widget& hit = *w;
                walking_ = false;   // the handlers end the walk (0x42792C) before the tree changes
                const auto fn = hit.activate;   // a copy: the handler may rebuild the tree (a new screen)
                fn(hit);
                return;
            }
            if (!walking_)
                return;
            if (w->child) {
                push(*w);
                activateWalk(w->child);
            }
        }
    }
    if (stack_.size() > 1)
        stack_.pop_back();
}

void Frontend::activate()
{
    walking_ = true;
    stackReset();
    if (root_)
        activateWalk(root_);
    const bool missed = walking_;   // (the walk ends itself when a widget took the click)
    walking_ = false;
    int sx, sy;
    if (missed && snapAim && root_ && snapPoint(sx, sy)) {   // Light Gun Edition: the nearest button / row instead
        const int px = px_, py = py_;
        px_ = sx, py_ = sy;
        walking_ = true;
        stackReset();
        activateWalk(root_);
        walking_ = false;
        px_ = px, py_ = py;
    }
}

void Frontend::snapCollect(Widget* w, int& best, int& bx, int& by)
{
    for (; w; w = w->next) {
        int x, y, ww, hh;
        absRect(*w, x, y, ww, hh);
        if (w->activate && w->hit && ww > 0 && hh > 0) {
            const int dx = px_ < x ? x - px_ : px_ >= x + ww ? px_ - (x + ww - 1) : 0;
            const int dy = py_ < y ? y - py_ : py_ >= y + hh ? py_ - (y + hh - 1) : 0;
            const int d = dx * dx + dy * dy;
            if (d < best) {   // a point on it that its own hit test takes (text widgets: only their letters)
                const int pts[3][2] = {{std::clamp(px_, x, x + ww - 1), std::clamp(py_, y, y + hh - 1)},
                                       {x + ww / 2, y + hh / 2},
                                       {x + std::min(ww - 1, 4), y + hh / 2}};
                for (const auto& q : pts)
                    if ((this->*w->hit)(*w, q[0], q[1])) {
                        best = d, bx = q[0], by = q[1];
                        break;
                    }
            }
        }
        if (w->child) {
            push(*w);
            snapCollect(w->child, best, bx, by);
        }
    }
    if (stack_.size() > 1)
        stack_.pop_back();
}

bool Frontend::snapPoint(int& x, int& y)
{
    int best = kSnapRange * kSnapRange + 1;
    x = y = -1;
    stackReset();
    if (root_)
        snapCollect(root_, best, x, y);
    return x >= 0;
}

// ---- 0x427F64: the width (sum of glyph widths + 1, space 8) and height ('A') of a string ----
// (lang: of its translation: measureRaw measures a text already translated)
void Frontend::measure(const std::string& s, uint32_t flags, int& w, int& h) const
{
    measureRaw(langOn() ? lang::tr(s) : s, flags, w, h);
}

void Frontend::measureRaw(const std::string& sIn, uint32_t flags, int& w, int& h) const
{
    h = spr_.height(glyph('A', flags));
    if (const render::TtfText* tf = lang::fontFor(sIn)) {   // lang: the language's font
        w = int(std::ceil(tf->width(sIn, classicEm(flags, h, tf))));
        return;
    }
    const std::string& s = lang::plain(sIn) ? sIn : pcBytes(sIn);
    w = 0;
    for (const char ch : s) {
        const int g = ch == ' ' ? 0 : glyph(uint8_t(ch), flags);
        if (!g)
            w += 8;
        else
            w += spr_.width(g) + 1;
    }
}

// ---- 0x427FE8 widget_draw_text ----
void Frontend::drawText(Widget& w)
{
    int x, y, ww, hh;
    absRect(w, x, y, ww, hh);
    std::string s = (w.flags & 0x1000) ? label(w.id) : w.text;
    if (langOn() && !w.raw)
        s = lang::tr(s);
    if (w.rtlRow && lang::rtl() && s.size() > 2 && s.compare(s.size() - 2, 2, " >") == 0)
        s = "< " + s.substr(0, s.size() - 2);   // (rtl: a sub-page's mark points left, before the value)
    int tw, th;
    measureRaw(s, w.flags, tw, th);
    const render::TtfText* tf = lang::fontFor(s);
    float shrink = 1.0f;   // leftovers: a translation wider than its cell (fitW): smaller
    if (tf && w.ttfScale < 1.0f) {   // (sweep2: rows closer than the font's height)
        shrink = w.ttfScale;
        tw = int(std::ceil(float(tw) * shrink));
    }
    if (tf && w.fitW > 0 && tw > w.fitW) {
        shrink *= float(w.fitW) / float(tw);
        tw = w.fitW;
    }
    if (w.flags & 0x20)
        x += (ww - tw) / 2;
    else if ((w.rtlRow ? rtlAlign(w.flags) : w.flags) & 0x04)
        x += ww - tw;
    if (w.flags & 0x40)
        y += (hh - th) / 2;
    else if (w.flags & 0x10)
        y += hh - th;
    if (langCheck)
        langOverflow(s, x, tw, ww, &w);
    if (!tf && !lang::plain(s))
        s = pcBytes(s);
    // gallery3: a tinted text (w.tint): the pixels the glyphs change get the colour, shaded by their brightness
    const int bx0 = std::max(0, x), by0 = std::max(0, y), bx1 = std::min(kWidth, x + tw + 2), by1 = std::min(kHeight, y + th + 2);
    std::vector<uint8_t> before;
    if (w.tint && bx1 > bx0 && by1 > by0)
        for (int j = by0; j < by1; ++j)
            before.insert(before.end(), &(*canvas_)[(size_t(j) * kWidth + size_t(bx0)) * 4],
                          &(*canvas_)[(size_t(j) * kWidth + size_t(bx1)) * 4]);
    if (tf)   // lang: the language's font
        ttfDraw(s, w.flags, x, y, th, false, tf, shrink);
    else
    for (const char ch : s) {
        const int g = ch == ' ' ? 0 : glyph(uint8_t(ch), w.flags);
        if (!g) {
            x += 8;
            continue;
        }
        if (ch == ')' && (w.flags & 0x800000) && !(w.flags & 0x430000))
            drawMirrored(g, x, y);   // ours: the small font's ')' (see drawHelp)
        else
            blit(g, x, y);
        x += spr_.width(g) + 1;
    }
    if (!before.empty()) {
        size_t k = 0;
        for (int j = by0; j < by1; ++j)
            for (int i = bx0; i < bx1; ++i, k += 4) {
                uint8_t* p = &(*canvas_)[(size_t(j) * kWidth + size_t(i)) * 4];
                if (std::equal(p, p + 4, &before[k])) continue;
                const float l = std::max({p[0], p[1], p[2]}) / 255.0f;
                p[0] = uint8_t(((w.tint >> 16) & 0xFF) * l), p[1] = uint8_t(((w.tint >> 8) & 0xFF) * l), p[2] = uint8_t((w.tint & 0xFF) * l);
            }
    }
}

// ours: a glyph drawn mirrored left to right (the 6 x 10 font's ')' 0x383 is a second '(' in the PC data)
void Frontend::drawMirrored(int g, int x, int y)
{
    const int gw = spr_.width(g), gh = spr_.height(g);
    const std::vector<uint8_t> img = spr_.image(g);
    for (int yy = 0; yy < gh && size_t(gw) * size_t(gh) * 4 <= img.size(); ++yy)
        for (int xx = 0; xx < gw; ++xx) {
            const uint8_t* p = &img[(size_t(yy) * size_t(gw) + size_t(gw - 1 - xx)) * 4];
            const int dx = x + xx, dy = y + yy;
            if (p[3] && dx >= 0 && dx < cw_ && dy >= 0 && dy < kHeight)
                std::copy(p, p + 3, &(*canvas_)[(size_t(dy) * cw_ + size_t(dx)) * 4]);
        }
}

// ---- 0x427CDC widget_draw_image (the hover picture while hovered) ----
void Frontend::drawImage(Widget& w)
{
    int x, y, ww, hh;
    absRect(w, x, y, ww, hh);
    const int id = (w.flags & 0x40000) && w.idHover != -1 ? w.idHover : w.id;
    const int iw = spr_.width(id), ih = spr_.height(id);
    if (w.flags & 0x20)
        x += (ww - iw) / 2;
    else if (w.flags & 0x04)
        x += ww - iw;
    if (w.flags & 0x40)
        y += (hh - ih) / 2;
    else if (w.flags & 0x10)
        y += hh - ih;
    // the original's draw scale (flags 0x100000: 1, else 2) goes to the render driver; not understood yet
    blit(id, x, y);
}

// ---- 0x428178: the pointer is on the text itself (its measured box) ----
bool Frontend::hitText(Widget& w, int px, int py)
{
    int x, y, ww, hh;
    absRect(w, x, y, ww, hh);
    const std::string s = (w.flags & 0x1000) ? label(w.id) : w.text;
    int tw, th;
    measure(s, w.flags, tw, th);
    if (langOn() && lang::fontFor(lang::tr(s))) {   // (sweep2: as drawText's smaller / fitted language font)
        tw = int(std::ceil(float(tw) * std::min(w.ttfScale, 1.0f)));
        if (w.fitW > 0) tw = std::min(tw, w.fitW);
    }
    if (w.flags & 0x20)
        x += (ww - tw) / 2;
    else if ((w.rtlRow ? rtlAlign(w.flags) : w.flags) & 0x04)
        x += ww - tw;
    if (w.flags & 0x40)
        y += (hh - th) / 2;
    else if (w.flags & 0x10)
        y += hh - th;
    return px >= x && px < x + tw && py >= y && py < y + th;
}

// ---- 0x428294: the pointer is in the widget's rectangle ----
bool Frontend::hitRect(Widget& w, int px, int py)
{
    int x, y, ww, hh;
    absRect(w, x, y, ww, hh);
    return px >= x && px < x + ww && py >= y && py < y + hh;
}

// ---- 0x427E70: the pointer is on the picture (its sprite's size, placed by the flags as 0x427CDC) ----
bool Frontend::hitImage(Widget& w, int px, int py)
{
    int x, y, ww, hh;
    absRect(w, x, y, ww, hh);
    const int iw = spr_.width(w.id), ih = spr_.height(w.id);
    if (w.flags & 0x20)
        x += (ww - iw) / 2;
    else if (w.flags & 0x04)
        x += ww - iw;
    if (w.flags & 0x40)
        y += (hh - ih) / 2;
    else if (w.flags & 0x10)
        y += hh - ih;
    return px >= x && px < x + iw && py >= y && py < y + ih;
}

// ---- 0x420904 / 0x420A10: the bony hand, its top-left at the pointer; frame 0x5CECA8 (the click's 4 frames) ----
void Frontend::drawCursor()
{
    drawHand(px_, py_);
}

// galfix (user 2026-10-26: the hand ~10 % smaller and more modern, both menu styles): the sprite at kHandScale with a
// bilinear filter (smooth edges, no hard pixel steps) over a soft shadow down and to the right, at handAlpha; the
// sprite's top left (the finger tip) stays at the pointer, as the PC draws it (0x420904)
void Frontend::drawHand(int x, int y)
{
    const int id = 0xF9 + cursorFrame_;
    const std::vector<uint8_t> img = spr_.image(id);
    const int iw = spr_.width(id), ih = spr_.height(id);
    if (iw <= 0 || ih <= 0 || img.size() < size_t(iw) * size_t(ih) * 4 || !canvas_)
        return;
    const float P = sc_ == 1 ? 1.0f : ps_;   // canvas pixels a menu unit
    const int pw = sc_ == 1 ? cw_ : rpw(), ph = sc_ == 1 ? kHeight : rph();
    const float k = kHandScale * P;          // canvas pixels a sprite pixel
    const float x0 = float(x) * P, y0 = float(y) * P;
    // the sprite premultiplied (its alpha is 0 / 255), sampled bilinear in sprite pixels (outside: clear)
    auto texel = [&](int u, int v, int c) -> float {
        if (u < 0 || v < 0 || u >= iw || v >= ih) return 0.0f;
        const uint8_t* s = &img[(size_t(v) * size_t(iw) + size_t(u)) * 4];
        const float a = s[3] ? 1.0f : 0.0f;
        return c == 3 ? a : a * float(s[c]) / 255.0f;
    };
    auto sample = [&](float u, float v, float out[4]) {
        u -= 0.5f, v -= 0.5f;
        const int u0 = int(std::floor(u)), v0 = int(std::floor(v));
        const float fu = u - float(u0), fv = v - float(v0);
        for (int c = 0; c < 4; ++c)
            out[c] = (texel(u0, v0, c) * (1 - fu) + texel(u0 + 1, v0, c) * fu) * (1 - fv) +
                     (texel(u0, v0 + 1, c) * (1 - fu) + texel(u0 + 1, v0 + 1, c) * fu) * fv;
    };
    // a premultiplied colour over the canvas pixel (straight alpha)
    auto over = [&](int px, int py, const float c[4], float alpha) {
        const float a = c[3] * alpha;
        if (px < 0 || py < 0 || px >= pw || py >= ph || a <= 0.002f) return;
        uint8_t* d = &(*canvas_)[(size_t(py) * size_t(pw) + size_t(px)) * 4];
        const float da = float(d[3]) / 255.0f, oa = a + da * (1.0f - a);
        for (int i = 0; i < 3; ++i)
            d[i] = uint8_t(std::clamp((c[i] * alpha * 255.0f + float(d[i]) * da * (1.0f - a)) / oa + 0.5f, 0.0f, 255.0f));
        d[3] = uint8_t(oa * 255.0f + 0.5f);
    };
    const float sh = 1.6f * P;   // the shadow's offset (canvas pixels), soft: a 3 x 3 average
    const int ix0 = int(std::floor(x0)), iy0 = int(std::floor(y0));
    const int ix1 = int(std::ceil(x0 + float(iw) * k + sh + P)), iy1 = int(std::ceil(y0 + float(ih) * k + sh + P));
    for (int py = std::max(0, iy0); py < std::min(ph, iy1); ++py)
        for (int px = std::max(0, ix0); px < std::min(pw, ix1); ++px) {
            float s = 0, t[4];
            for (int j = -1; j <= 1; ++j)
                for (int i = -1; i <= 1; ++i) {
                    sample((float(px) + 0.5f - x0 - sh + float(i) * P * 0.6f) / k, (float(py) + 0.5f - y0 - sh + float(j) * P * 0.6f) / k, t);
                    s += t[3];
                }
            const float shadow[4] = {0, 0, 0, s / 9.0f};
            over(px, py, shadow, 0.45f * handAlpha);
            sample((float(px) + 0.5f - x0) / k, (float(py) + 0.5f - y0) / k, t);
            over(px, py, t, handAlpha);
        }
}

// ---- ours: the help line (the 6 x 10 PC font, centred at the bottom; split in two at a space when too wide) ----
void Frontend::drawHelp()
{
    if (help_.empty() || !canvas_)
        return;
    const uint32_t f = 0x800000;
    // lang: the translation (help: the text drawn; English: help_ itself), wrapped by the language's font
    const std::string help = langOn() ? lang::tr(help_) : help_;
    int tw, th;
    measureRaw(help, f, tw, th);
    tw -= 4 * int(std::count(help.begin(), help.end(), ' '));   // (uxpolish: the small font's spaces are 4, not 8)
    std::vector<std::string> lines{help};
    if (const render::TtfText* tf = lang::fontFor(help)) {
        lines = tf->wrap(help, classicEm(f, th, tf), float(cw_ - 16));
        if (langCheck && lines.size() > 2)
            langOverflow(help, 0, cw_ * 2, cw_, nullptr);
    } else if (tw > cw_ - 16) {
        // the space nearest the middle (uxpolish: not one before a number or "/", which read as cut off)
        size_t cut = std::string::npos;
        auto cost = [&](size_t i) {
            const char nx = i + 1 < help.size() ? help[i + 1] : ' ';
            const bool bad = (nx >= '0' && nx <= '9') || nx == '/' || (i > 0 && help[i - 1] == '/');
            return std::abs(int(i) - int(help.size() / 2)) + (bad ? 1000 : 0);
        };
        for (size_t i = 0; i < help.size(); ++i)
            if (help[i] == ' ' && (cut == std::string::npos || cost(i) < cost(cut)))
                cut = i;
        if (cut != std::string::npos)
            lines = {help.substr(0, cut), help.substr(cut + 1)};
    }
    // (uxpolish: one line higher than before when the controls line hintFor() sits under it)
    const bool hint = !hintFor().empty();
    int y = lines.size() > 1 ? (hint ? 0x1B8 : 0x1C4) : (hint ? 0x1C2 : 0x1CA);
    for (const std::string& s : lines) {
        drawSmallRaw(s, y, false);
        y += th + 4;
    }
}

// ours: one centred line in the 6 x 10 PC font (dim: at 60 %, the controls line)
void Frontend::drawSmall(const std::string& s, int y, bool dim)
{
    drawSmallRaw(langOn() ? lang::tr(s) : s, y, dim);
}

void Frontend::drawSmallRaw(const std::string& sIn, int y, bool dim)
{
    const uint32_t f = 0x800000;
    int tw, th;
    measureRaw(sIn, f, tw, th);
    if (const render::TtfText* tf = lang::fontFor(sIn)) {   // lang: the language's font
        if (langCheck && tw > cw_ - 4)
            langOverflow(sIn, 0, tw, cw_, nullptr);
        ttfDraw(sIn, f, std::max(2, (cw_ - tw) / 2), y, th, dim, tf);
        return;
    }
    const std::string& s = lang::plain(sIn) ? sIn : pcBytes(sIn);
    tw -= 4 * int(std::count(s.begin(), s.end(), ' '));   // (measure's spaces are 8 wide, ours 4)
    int x = std::max(2, (cw_ - tw) / 2);
    for (const char ch : s) {
        const int g = ch == ' ' ? 0 : glyph(uint8_t(ch), f);
        if (!g) {
            x += ch == ' ' ? 4 : 8;   // (the small font's space: half the big fonts' 8)
            continue;
        }
        const int gw = spr_.width(g), gh = spr_.height(g);
        if (ch == ')' || dim) {   // the small font's ')' (0x383) is a second '(' in the PC data: '(' mirrored
            const std::vector<uint8_t> img = spr_.image(g);
            for (int yy = 0; yy < gh && size_t(gw) * size_t(gh) * 4 <= img.size(); ++yy)
                for (int xx = 0; xx < gw; ++xx) {
                    const uint8_t* p = &img[(size_t(yy) * size_t(gw) + size_t(ch == ')' ? gw - 1 - xx : xx)) * 4];
                    const int dx = x + xx, dy = y + yy;
                    if (!p[3] || dx < 0 || dx >= cw_ || dy < 0 || dy >= kHeight)
                        continue;
                    uint8_t* d = &(*canvas_)[(size_t(dy) * cw_ + size_t(dx)) * 4];
                    for (int c = 0; c < 3; ++c)
                        d[c] = dim ? uint8_t(p[c] * 2 / 5) : p[c];   // (menu3: dimmer, was 3 / 5)
                }
        } else if (x + gw <= cw_)
            blit(g, x, y);
        x += gw + 1;
    }
}

// ---- lang: the language's texts on the Classic screens ----
// the font's em (pixels) for the PC font of flags (its capitals th high); the small fonts at least 12 pixels (Hangul /
// kana / CJK are unreadable smaller)
float Frontend::classicEm(uint32_t flags, int th, const render::TtfText* tf) const
{
    const float em = lang::pxForCap(float(std::max(th, 1)), tf);
    return (flags & 0x800000) ? std::max(em, 12.0f) : std::max(em, 9.0f);
}

// a translated text as the PC fonts' bytes (Latin-1: their accented letters); '?' for what they lack
std::string Frontend::pcBytes(const std::string& s)
{
    std::string out;
    for (size_t i = 0; i < s.size();) {
        const uint32_t cp = render::utf8Next(s, i);
        out += cp <= 0xFF ? char(uint8_t(cp)) : '?';
    }
    return out;
}

// the PC font's colour (its brightest pixel of 'A': white, the hover's yellow, grey ...)
uint32_t Frontend::fontColour(uint32_t flags) const
{
    auto it = fontColour_.find(flags);
    if (it != fontColour_.end())
        return it->second;
    uint32_t best = 0xFFFFFF;
    const int g = glyph('A', flags);
    const std::vector<uint8_t> img = spr_.image(g);
    int bl = -1;
    for (size_t i = 0; i + 3 < img.size(); i += 4)
        if (img[i + 3]) {
            const int l = img[i] * 3 + img[i + 1] * 6 + img[i + 2];
            if (l > bl) bl = l, best = uint32_t(img[i]) << 16 | uint32_t(img[i + 1]) << 8 | img[i + 2];
        }
    fontColour_[flags] = best;
    return best;
}

// s (translated) in the language's font: its capitals' top at y (th: the PC font's capitals), in the PC font's
// colour (dim: 40 %), with a dark edge (the big fonts) or a drop shadow (the 6 x 10 font) as the PC's letters have
void Frontend::ttfDraw(const std::string& s, uint32_t flags, int x, int y, int th, bool dim, const render::TtfText* tf,
                       float shrink)
{
    if (!tf || !canvas_)
        return;
    const float S = sc_ > 1 ? ps_ : 1.0f;
    const int CW = sc_ > 1 ? rpw() : cw_, CH = sc_ > 1 ? rph() : kHeight;
    const float em = classicEm(flags, th, tf) * S * shrink;
    const float tw = tf->width(s, em);
    const int m = 3;
    const int LX = int(std::floor(float(x) * S)) - m, LY = int(std::floor((float(y) - float(th) * 0.6f) * S)) - m;
    const int LW = int(std::ceil(tw)) + 2 * m + 2, LH = int(std::ceil((float(th) * 1.6f) * S + em * 0.4f)) + 2 * m;
    if (LW <= 0 || LH <= 0)
        return;
    std::vector<float> cov(size_t(LW) * size_t(LH), 0.0f);
    tf->draw(s, em, float(x) * S, float(y + th) * S, [&](int px, int py, float a) {
        px -= LX, py -= LY;
        if (px >= 0 && py >= 0 && px < LW && py < LH) {
            float& c = cov[size_t(py) * size_t(LW) + size_t(px)];
            c = std::min(1.0f, c + a);
        }
    });
    uint32_t rgb = fontColour(flags);
    if (dim)
        rgb = (((rgb >> 16) & 255) * 2 / 5) << 16 | (((rgb >> 8) & 255) * 2 / 5) << 8 | ((rgb & 255) * 2 / 5);
    const bool small = (flags & 0x800000) != 0;
    const int r = std::max(1, int(std::lround(S)));
    auto blend = [&](int px, int py, uint32_t c, float a) {
        if (px < 0 || py < 0 || px >= CW || py >= CH || a <= 0.0f)
            return;
        uint8_t* d = &(*canvas_)[(size_t(py) * size_t(CW) + size_t(px)) * 4];
        const float k = std::min(1.0f, a);
        d[0] = uint8_t(float(d[0]) * (1 - k) + float((c >> 16) & 255) * k + 0.5f);
        d[1] = uint8_t(float(d[1]) * (1 - k) + float((c >> 8) & 255) * k + 0.5f);
        d[2] = uint8_t(float(d[2]) * (1 - k) + float(c & 255) * k + 0.5f);
        if (sc_ > 1)
            d[3] = uint8_t(std::max(float(d[3]), k * 255.0f));
    };
    for (int j = 0; j < LH; ++j)
        for (int i = 0; i < LW; ++i) {
            float edge = 0;
            if (small) {   // a drop shadow
                const int u = i - r, v = j - r;
                if (u >= 0 && v >= 0) edge = cov[size_t(v) * size_t(LW) + size_t(u)];
            } else         // a dark edge r pixels round
                for (int dy = -r; dy <= r && edge < 1.0f; ++dy)
                    for (int dx = -r; dx <= r; ++dx) {
                        const int u = i + dx, v = j + dy;
                        if (u >= 0 && v >= 0 && u < LW && v < LH) edge = std::max(edge, cov[size_t(v) * size_t(LW) + size_t(u)]);
                    }
            const float a = cov[size_t(j) * size_t(LW) + size_t(i)];
            if (edge > 0.01f) blend(LX + i, LY + j, 0x000000, edge * 0.85f);
            if (a > 0.01f) blend(LX + i, LY + j, rgb, a);
        }
}

void Frontend::recordTexts()
{
    if (!lang::on())
        return;
    for (const auto& w : pool_) {
        if (!w->raw) lang::tr(w->text);
        if (w->flags & 0x1000) lang::tr(label(w->id));
        lang::tr(w->help);
        lang::tr(w->value);
    }
    for (const Extra& e : extras) {
        if (!e.raw) lang::tr(e.name);
        lang::tr(e.help);
        if (e.off) lang::tr(e.off());
        if (e.value) lang::tr(e.value());
        for (const std::string& c : e.choices) lang::tr(c);
    }
    lang::tr(hintFor());
    lang::tr(extrasTitle);
    for (const std::string& t : tabs) lang::tr(t);
}

// --lang-check: a text wider than its box (or the screen): listed with the screen it is on
void Frontend::langOverflow(const std::string& s, int x, int tw, int ww, const Widget* w)
{
    (void)w;
    if (!util::utf8Valid(s)) {   // (misc18: a cut through a letter; reported as BROKEN)
        overflows.push_back({s, -1, "screen " + std::to_string(screen_) + (remakeKey_.empty() ? "" : " (" + remakeKey_ + ")")});
        return;
    }
    const int over = std::max(ww > 0 ? tw - ww : 0, std::max(-x, x + tw - cw_));
    if (over <= 2 || s.empty())
        return;
    overflows.push_back({s, over, "screen " + std::to_string(screen_) + (remakeKey_.empty() ? "" : " (" + remakeKey_ + ")")});
}

// ours (uxpolish; menu3: shorter and dimmer, user 2026-10-21): the controls line at the very bottom of each menu
// screen, the keyboard / pad keys only (the mouse points and clicks everywhere; South / Start = Enter, East / Back =
// Escape: Pads::menuKey). The top of the main menu quits with Escape; a group (More Modes / Extras) goes back
std::string Frontend::hintFor() const
{
    switch (screen_) {
    case 1: return mainGroup_ ? "Enter/A Choose    Esc/B Back" : "Enter/A Choose    Esc/B Quit";
    case 15: return "Type a name    Backspace/B Delete    Enter Done";
    case 16: return "";   // PC Mode: the original's two panels fill the screen (and the 4P panels' own keys)
    case 17: case 19: return "";   // the Gallery / Cut-scenes: their own keys line at the bottom
    case 20: return "Up/Down Pick    Enter/A Play    Esc/B Back";
    case 21: return "Enter/A Choose    Esc/B No";
    case 100: return "Enter/A Change    Right-click List    Esc/B Back";   // (optui: a row's dropdown)
    default: return "Enter/A Choose    Esc/B Back";
    }
}

// ours (uxpolish): the EXPERIMENTAL tag (6 x 10 font) at (x, y) in parent; 0 when it would pass maxRight
int Frontend::experimentalTag(Widget* parent, int x, int y, int maxRight)
{
    static const std::string kTag = "EXPERIMENTAL";
    int tw, th;
    measure(kTag, 0x800000, tw, th);
    if (x + tw > maxRight)
        return 0;
    if (lang::rtl() && parent)   // rtl: left of the label (the row mirrored)
        x = parent->w - x - tw;
    Widget* t = widgetNew();
    addChild(parent, t);
    setRect(t, x, y, tw, th);
    setText(t, kTag, 0x800000);
    return tw;
}

// ours: a label drawn yellow always (the current tab): the hover's colour without the hover
void Frontend::drawTextLit(Widget& w)
{
    w.flags |= 0x40000;
    drawText(w);
}

// ---- ours: the settings' categories as a strip of white 24 labels across the screen (the current one yellow) ----
void Frontend::tabStrip(Widget* root, int y, int current)
{
    if (tabs.empty())
        return;
    const int n = int(tabs.size());
    std::vector<int> wid(size_t(n), 0);
    int total = 0, th = 0;
    for (int i = 0; i < n; ++i) {
        measure(tabs[size_t(i)], 0x20000, wid[size_t(i)], th);
        total += wid[size_t(i)];
    }
    const int avail = cw_ - 32;
    const int gap = n > 1 ? std::clamp((avail - total) / (n - 1), 8, 36) : 0;
    int x = (cw_ - (total + gap * (n - 1))) / 2;
    const bool rtl = lang::rtl();   // (rtl: the first tab at the right)
    for (int i = 0; i < n; ++i) {
        Widget* t = widgetNew();
        addChild(root, t);
        setRect(t, rtl ? cw_ - x - wid[size_t(i)] : x, y, wid[size_t(i)], th);
        setText(t, tabs[size_t(i)], 0x20002);
        if (i == current)
            t->draw = &Frontend::drawTextLit;
        t->help = i == current ? tabs[size_t(i)] + " settings (this page)" : "Open the " + tabs[size_t(i)] + " settings";
        setHandlers(t, [this, i](Widget&) {
            walking_ = false;
            if (onTab) {
                const auto fn = onTab;   // a copy: it rebuilds the page
                fn(i);
            }
        }, &Frontend::hitRect);
        x += wid[size_t(i)] + gap;
    }
}

// ---- the main menu's items (remakemenu: the model both styles lay out; was screenMainMenu's own list) ----
std::vector<Frontend::MainItem> Frontend::mainMenuItems()
{
    using Item = MainItem;
    std::vector<Item> items;
    auto pick = [this](int k) { return std::function<void()>([this, k]() { if (onMainMenu) onMainMenu(k); }); };
    // menu2 (user 2026-10-21: "getting cluttered"): the top keeps the PC's own items in the PC's order (Arcade, PC Mode,
    // Boss Mode, High Scores = Ranking, Options, Quit) plus two groups, More Modes (Chapter Select, Co-op, Sonic Mode)
    // and Extras (Gallery, Jukebox); a group is the same list with its name (yellow) first and Back last (Escape).
    // The pages opened from a group come back to it (screenMainMenu shows mainGroup_; start() resets it)
    auto group = [this](int g) {
        return std::function<void()>([this, g]() {
            mainGroup_ = g;
            screenMainMenu();
        });
    };
    const int grp = debugItems_ ? 0 : mainGroup_;
    if (grp == 1) {
        items.push_back({-1, "More Modes", "", nullptr});
        items.back().heading = true;
        items.push_back({-1, "Chapter Select", "Start at any chapter", [this]() { screenChapters(); }});
        items.push_back({-1, "Co-op", "2 players, 4 players or online",
                         onCoop ? std::function<void()>([this]() { const auto f = onCoop; f(); }) : nullptr});
        if (sonicValue)
            items.push_back({-1, "Sonic Mode: " + sonicValue(), "Faster play, own high scores (click: next preset)",
                             [this]() {
                                 if (onSonic) onSonic();
                                 screenMainMenu();
                             }});
        items.push_back({-1, "Back", "Back to the main menu (Escape)", group(0)});
    } else if (grp == 2) {
        items.push_back({-1, "Extras", "", nullptr});
        items.back().heading = true;
        items.push_back({-1, "Gallery", openGallery ? "The characters and creatures in 3D" : "Gallery: needs the arcade files",
                         openGallery ? std::function<void()>([this]() { const auto f = openGallery; f(); }) : nullptr});
        items.push_back({-1, "Jukebox", openJukebox ? "The music, sound effects and voices" : "Jukebox: no sound files or sound device",
                         openJukebox ? std::function<void()>([this]() { const auto f = openJukebox; f(); }) : nullptr});
        items.push_back({-1, "Back", "Back to the main menu (Escape)", group(0)});
    } else {
        // playerpick: Arcade and PC Mode ask the players first (screen 22; Back: the main menu)
        auto ask = [this](const char* name, std::function<void()> go) {
            return go ? std::function<void()>([this, name, go]() {
                std::string m = name;
                if (std::string(name) == "Arcade" && sonicValue && sonicValue() != "Off")
                    m += " - Sonic Mode " + sonicValue();
                askPlayers(m, go, [this]() { screenMainMenu(); });
            }) : nullptr;
        };
        items.push_back({1, "", "The whole game from Chapter 1, as the arcade", ask("Arcade", pick(0))});
        // Boss Mode / PC Mode: their workers' startBossMode / startPcMode (grey while unset, or onMainMenu(2 / 1) when
        // mainMenuEnabled says so)
        auto mode = [&](const std::function<void()>& start, int k) {
            return start ? std::function<void()>([start]() { const auto f = start; f(); }) : mainMenuEnabled[k] ? pick(k) : nullptr;
        };
        const auto boss = mode(startBossMode, 2), pc = ask("PC Mode", mode(startPcMode, 1));
        items.push_back({2, "", pc ? "The PC version's own mode" : "PC Mode: coming (not ported yet)", pc});
        items.push_back({0x42, "", boss ? "Fight the bosses one by one" : "Boss Mode: coming (not ported yet)", boss});
        items.push_back({-1, "More Modes", "Chapter Select, Co-op, Sonic Mode", group(1)});
        items.push_back({-1, "Extras", "Gallery, Jukebox", group(2)});
        items.push_back({0x43, "", "The top 10 tables", mainMenuEnabled[3] ? std::function<void()>([this]() { screenRanking(); }) : nullptr});
        items.push_back({6, "", "Difficulty, lives, volumes and all settings",
                         mainMenuEnabled[4] ? std::function<void()>([this]() { screenOptions(); }) : nullptr});
        // uxpolish: Quit (and Escape / a pad's B) asks first, with the PC's own "Are you sure?" / Yes / No labels
        items.push_back({7, "", "Leave the game (asks first)", [this]() { screenQuit(); }});
    }
    // 0x421342..0x42142C: with the debug flag 0x5CD374 (Ctrl + SKIDMARX): chapter_select_check 0x422808, then Cheat
    // (handler 0x422784 -> 0x4228CC), Edit Char Stats (0x426188 -> 0x426190) and, with 0x5CE898 (Ctrl + CREATURE),
    // Creature Test (0x422230 -> 0x422324), as text (widget_set_text, not label ids)
    if (debugItems_) {
        auto add = [&](const char* text, std::function<void()> act) {
            items.push_back({-1, text, "", std::move(act)});
            items.back().debug = true;
        };
        cheatStage_ = std::clamp(cheatStage_, 0, 3);   // chapter_select_check 0x422808 (clamps 0xD5B350 / 0xD5B354)
        add("Cheat", [this]() { screenCheat(); });
        add("Edit Char Stats", onEditCharStats ? std::function<void()>([this]() { const auto f = onEditCharStats; f(); }) : nullptr);
        if (creatureItem_)
            add("Creature Test", [this]() { screenCreatureTest(); });
    }
    return items;
}

// ---- screen_main_menu 0x4210FC (screen 1) ----
void Frontend::screenMainMenu()
{
    if (style == kStyleRemake) {   // remakemenu: the same items, the Remake style's layout
        remakeMainMenu();
        return;
    }
    screen_ = 1;
    idleReset_ = true;   // 0x421119: the idle timer starts with the screen
    freeTree();
    texts_.clear();
    texts_.reserve(4);
    Widget* root = widgetNew();
    root_ = root;
    setRect(root, 0, 0, 0, 0);
    setImage(root, 0xD3, -1, 0x20000A);   // the logo over the mansion (640 x 480)
    Widget* list = widgetNew();
    addChild(root, list);
    // ReWrithe: nine items (the PC's six: list at 0xD0, 32 apart); 0xC8 + 8 x 28 + 24 = 448: the help line fits under
    setRect(list, 0, 0xC8, 0, 0x118);
    using Item = MainItem;
    std::vector<Item> items = mainMenuItems();
    size_t mainCount = 0;
    while (mainCount < items.size() && !items[mainCount].debug)
        ++mainCount;
    // eight items at most: 28 apart from 0xC8 (the PC: 32 from 0xD0); SKIDMARX: 19 apart, so Cheat / Edit Char Stats /
    // Creature Test fit under Quit (pc_cheats.md)
    const int step = debugItems_ ? 19 : 28;
    int y = 0;
    for (size_t k = 0; k < mainCount; ++k) {
        Item& it = items[k];
        Widget* item = widgetNew();
        addChild(list, item);
        // ReWrithe (user, 2026-10-17): PC Mode and Boss Mode stay visible, greyed out (the grey 24 font) and not
        // selectable while the teaser runs the arcade logic; the original has them white with handlers
        const uint32_t fl = it.act || it.heading ? 0x20020 : 0xA0020;   // white (grey) 24, centred
        if (it.label >= 0)
            setLabel(item, it.label, fl);
        else {
            texts_.push_back(it.text);
            setText(item, texts_.back(), fl);
        }
        setRect(item, 0, y, 0, 24);
        item->help = it.help;
        if (it.heading)   // menu2: a group's heading, yellow, not a button
            item->draw = &Frontend::drawTextLit;
        if (it.act)
            setHandlers(item, [this, act = it.act](Widget&) {
                walking_ = false;   // 0x42792C before a new screen
                act();
            }, &Frontend::hitText);
        if (k + 1 == mainCount)
            item->tag = 1;   // 0x427CB0: Escape activates Quit
        if (debugItems_)
            item->help.clear();   // (the help line's place holds the debug items)
        y += step;
    }
    // the debug items (mainMenuItems): Quit's y + 0x23, then 0x19 apart; white 24, centred. ReWrithe: our ten items are
    // 19 apart then (not 25) so the three fit in 480
    int dy = y - step + 0x23;
    for (size_t k = mainCount; k < items.size(); ++k) {
        const Item& it = items[k];
        Widget* item = widgetNew();
        addChild(list, item);
        texts_.push_back(it.text);
        setText(item, texts_.back(), it.act ? 0x20020 : 0xA0020);
        setRect(item, 0, dy, 0, 24);
        if (it.act)
            setHandlers(item, [this, act = it.act](Widget&) {
                walking_ = false;   // 0x42792C
                act();
            }, &Frontend::hitText);
        dy += 0x19;
    }
    addVersionTag();
}

// relprep: the version (generated/hotd_version.h, from CMakeLists.txt's project VERSION) in the main menu's bottom-left
// corner (the Remake's: top-right, its prompts are at the bottom), small, not translated
void Frontend::addVersionTag()
{
    // the update check (util/update_check.h): once per run, when the main menu is built after a newer release was found
    static bool asked = false;
    std::string tag;
    if (!asked && util::updateAvailable(tag)) {
        asked = true;
        const std::string msg = "A new version (" + tag + ") of The House of the Dead: ReWrithe is available (you have " +
                                std::string(HOTD_VERSION_TEXT) + ").\n\nOpen the download page? Unzip the new version over "
                                "this folder; your settings are kept.";
        const SDL_MessageBoxButtonData buttons[] = {{SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Open download page"},
                                                    {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Not now"}};
        const SDL_MessageBoxData box = {SDL_MESSAGEBOX_INFORMATION, nullptr, "Update available", msg.c_str(), 2, buttons, nullptr};
        int pick = 0;
        if (SDL_ShowMessageBox(&box, &pick) && pick == 1) SDL_OpenURL(util::kReleasesPage);
    }
    if (style == kStyleRemake) {
        Widget* v = rWidget(cw_ - 212, 6, 200, 14, &Frontend::drawRText, HOTD_VERSION_TEXT);
        v->id = 2;   // right-aligned
        v->user = int(0x8A827A);
        v->raw = true;
        return;
    }
    Widget* v = widgetNew();
    addChild(root_, v);
    setRect(v, 6, kHeight - 14, 200, 12);
    texts_.push_back(HOTD_VERSION_TEXT);
    setText(v, texts_.back(), 0x800000);   // the small font, left-aligned
    v->raw = true;
}

// ---- screen 21 (ours, uxpolish): Quit's question. The string table has the PC's "Are you sure?" (8), "Yes" (9) and
// "No" (10) with the Y / N tags 2 / 3 (0x427CB0); ours: the main menu's backdrop, the question in white 32, Yes / No in
// white 24 under it; Escape (a pad's B) is No: Escape on the main menu never ends the game by itself ----
void Frontend::screenQuit()
{
    if (style == kStyleRemake) {
        remakeQuit();
        return;
    }
    screen_ = 21;
    freeTree();
    texts_.clear();
    Widget* root = widgetNew();
    root_ = root;
    setRect(root, 0, 0, 0, 0);
    setImage(root, 0xD3, -1, 0x20000A);
    Widget* q = widgetNew();
    addChild(root, q);
    setRect(q, 0, 0xE0, 0, 32);
    setLabel(q, 8, 0x10020);   // "Are you sure?"
    Widget* yes = widgetNew();
    addChild(root, yes);
    setRect(yes, 0, 0x120, 0, 24);
    setLabel(yes, 9, 0x20020);
    yes->tag = 2;
    yes->help = "Leave the game";
    setHandlers(yes, [this](Widget&) {
        walking_ = false;
        if (onMainMenu) onMainMenu(5);   // 0x420F80
    }, &Frontend::hitText);
    Widget* no = widgetNew();
    addChild(root, no);
    setRect(no, 0, 0x140, 0, 24);
    setLabel(no, 10, 0x20020);
    no->tag = 1;   // Escape
    no->help = "Back to the main menu (Escape)";
    setHandlers(no, [this](Widget&) {
        walking_ = false;
        screenMainMenu();
    }, &Frontend::hitText);
}

// ---- screen 22 (ours, playerpick): the players' quick choice before a local game (frontend.h askPlayers) ----
void Frontend::askPlayers(const std::string& mode, std::function<void()> go, std::function<void()> back)
{
    if (!playersAsk || !playersAsk() || !playersGet || !playersSet) {   // (online, or no host hooks: as before)
        if (go) go();
        return;
    }
    pickMode_ = mode;
    pickGo_ = std::move(go);
    // Back: the page before with the hand / focus where it was (the item that opened the choice)
    pickBack_ = [this, back = std::move(back), x = px_, y = py_]() {
        px_ = x, py_ = y;
        if (back) back();
        else screenMainMenu();
    };
    int n = 1, bot[4] = {};
    playersGet(n, bot);
    n = std::clamp(n, 1, 4);
    for (int p = 1; p < 4; ++p) {
        pickBot_[p] = std::clamp(bot[p], 0, 3);
        if (pickBot_[p] > 0)
            pickLevel_[p] = pickBot_[p];
    }
    pickBot_[0] = 0;
    if (n < 2 && pickBot_[1] > 0)   // (1 player with player 2 a computer teammate: two in the game)
        n = 2;
    pickN_ = n;
    pickNew_ = true;
    screenPlayers();
}

std::string Frontend::pickSeatText(int p) const
{
    static const char* const kLevel[4] = {"Human", "CPU Easy", "CPU Normal", "CPU Hard"};
    return "Player " + std::to_string(p + 1) + ": " + kLevel[std::clamp(pickBot_[p], 0, 3)];
}

void Frontend::pickPlayers(int n, bool toStart)
{
    pickN_ = std::clamp(n, 1, 4);
    pickNew_ = pickNew_ || toStart;   // (a number key / shoulder button: Start next, one press away)
    walking_ = false;
    screenPlayers();
}

void Frontend::pickSeat(int p)
{
    if (p < 1 || p > 3)
        return;
    pickBot_[p] = pickBot_[p] > 0 ? 0 : std::clamp(pickLevel_[p], 1, 3);
    walking_ = false;
    screenPlayers();
}

void Frontend::pickStart()
{
    walking_ = false;
    int bot[4] = {0, pickBot_[1], pickBot_[2], pickBot_[3]};
    if (pickN_ < 2)
        bot[1] = 0;   // (player 2's CPU plays whenever it is set: 1 player = alone)
    if (playersSet)
        playersSet(pickN_, bot);
    if (pickGo_) {
        const auto f = pickGo_;   // (a copy: the host may rebuild the screen)
        f();
    }
}

void Frontend::screenPlayers()
{
    if (style == kStyleRemake) {
        remakePlayers();
        return;
    }
    screen_ = 22;
    freeTree();
    texts_.clear();
    texts_.reserve(12);
    Widget* root = widgetNew();
    root_ = root;
    setRect(root, 0, 0, 0, 0);
    setImage(root, 0xD1, -1, 0x20000A);   // the hall without the logo
    auto text = [&](const std::string& s, int x, int y, int w, int h, uint32_t flags) {
        Widget* t = widgetNew();
        addChild(root, t);
        setRect(t, x, y, w, h);
        texts_.push_back(s);
        setText(t, texts_.back(), flags);
        return t;
    };
    text(pickMode_, 0, 0x40, 0, 32, 0x10020);   // the game (white 32)
    // "Players" and the four numbers on one row (white 32; the chosen one yellow), the 4 players mode's tag under them
    text("Players", 0x70, 0x90, 0x70, 32, 0x10004);
    for (int k = 1; k <= 4; ++k) {
        Widget* b = text(std::to_string(k), 0x100 + (k - 1) * 0x30, 0x90, 0x30, 32, 0x10020);
        if (k == pickN_)
            b->tint = 0xF8D040;
        b->help = k == 1 ? "1 player" : k == 2 ? "2 players (player 2: a person or the computer)"
                                               : std::to_string(k) + " players: the 4 players mode (players 2-4 join with their trigger)";
        setHandlers(b, [this, k](Widget&) { pickPlayers(k); }, &Frontend::hitRect);
    }
    // (Light Gun Edition: no "Experimental" tag for the 4 players mode)
    int y = 0xDC;
    for (int p = 1; p < pickN_; ++p) {   // Human / CPU for each seat past player 1 (white 24, centred)
        Widget* s = text(pickSeatText(p), 0, y, 0, 24, 0x20020);
        s->help = "Player " + std::to_string(p + 1) + ": you or the computer (click to switch; CPU level: Options > Co-op)";
        setHandlers(s, [this, p](Widget&) { pickSeat(p); }, &Frontend::hitText);
        y += 28;
    }
    y += 16;
    Widget* start = text("Start", 0, y, 0, 24, 0x20020);
    start->tag = 2;
    start->help = "Start " + pickMode_ + " with " + std::to_string(pickN_) + (pickN_ == 1 ? " player" : " players");
    setHandlers(start, [this](Widget&) { pickStart(); }, &Frontend::hitText);
    Widget* back = text("Back", 0, y + 0x20, 0, 24, 0x20020);
    back->tag = 1;   // Escape
    back->help = "Back (Escape)";
    setHandlers(back, [this](Widget&) {
        walking_ = false;
        if (pickBack_) {
            const auto f = pickBack_;
            f();
        } else
            screenMainMenu();
    }, &Frontend::hitText);
    if (pickNew_) {   // (the hand onto Start: Enter / A / a click without moving the mouse starts)
        pickNew_ = false;
        setPointer(cw_ / 2, y + 12);
    }
}

// ---- frontend_input 0x41DA18 (0x41DC3C..0x41DD38): the secret codes ----
// While neither Ctrl (DIK 0x1D / 0x9D) is held the 9-byte buffer 0x5CE89C is reset to "00000000" (0x5A1198); with
// Ctrl held each key edge in the list 0x5CE868 shifts the buffer left and puts the key's character (table 0x5CED9C)
// in place 7; then strcmp (0x571C79) with "SKIDMARX" (0x5A118C) toggles the debug flag 0x5CD374, with "CREATURE"
// (0x5A11A4) the flag 0x5CE898; either resets the buffer, calls 0x42792C and rebuilds the main menu (0x4210FC) from
// whatever screen is up. Tip: RUNESA (YouTube comment, 2026-10-20); docs/teaser_status/pc_cheats.md
void Frontend::secretCodes(const FrontendInput& in)
{
    if (!in.ctrl) {
        std::memcpy(codeBuf_, "00000000", 9);
        return;
    }
    for (char c : in.typed) {
        std::memmove(codeBuf_, codeBuf_ + 1, 7);
        codeBuf_[7] = c;
        codeBuf_[8] = 0;
        const bool skid = std::strcmp(codeBuf_, "SKIDMARX") == 0;
        const bool creature = !skid && std::strcmp(codeBuf_, "CREATURE") == 0;
        if (!skid && !creature)
            continue;
        std::memcpy(codeBuf_, "00000000", 9);
        if (skid)
            debugItems_ = !debugItems_;
        else
            creatureItem_ = !creatureItem_;
        mainGroup_ = 0;   // (menu2: the debug items sit under Quit, on the top)
        walking_ = false;
        screenMainMenu();
        return;
    }
}

namespace {
const char* const kPcStage[4] = {"Stage 01 ", "Stage 02 ", "Stage 03 ", "Stage 04 "};   // 0x5CE918 (10 bytes each)
constexpr int kPcScenes[4] = {20, 16, 8, 5};   // the scene names per stage (0x5CEBA4's lists)
const char* const kPcScene1[20] = {"Scene 000", "Scene 001", "Scene 002", "Scene 003", "Scene 01 ", "Scene 02 ",
    "Scene 03 ", "Scene 04 ", "Scene 05 ", "Scene 06 ", "Scene 07 ", "Scene 08 ", "Scene 09 ", "Scene 10 ", "Scene 11 ",
    "Scene 12 ", "Scene 13 ", "Scene 14 ", "Scene 15 ", "Scene 16 "};
const char* const kPcScene2[16] = {"Scene 000", "Scene 001", "Scene 01 ", "Scene 020", "Scene 021", "Scene 03 ",
    "Scene 040", "Scene 041", "Scene 05 ", "Scene 06 ", "Scene 07 ", "Scene 08 ", "Scene 09 ", "Scene 10 ", "Scene 11 ",
    "Scene 15 "};
const char* const kPcScene3[8] = {"Scene 00 ", "Scene 01 ", "Scene 02 ", "Scene 03 ", "Scene 04 ", "Scene 05 ",
    "Scene 06 ", "Scene 07 "};
const char* const kPcScene4[5] = {"Scene 00 ", "Scene 01 ", "Scene 02 ", "Scene 02A", "Scene 03 "};
const char* const* const kPcScenesOf[4] = {kPcScene1, kPcScene2, kPcScene3, kPcScene4};

const char* const kPcBodyNames[94] = {   // THOTD.EXE 0x5F10E0
    "BO_dkiller_kihon","BO_mon_kihon","BO_tom","BO_ebita","BO_ebitb","BO_mr_g","BO_molb","BO_frog_ct","BO_dkiller_black",
    "BO_mon_black","BO_syndy","BO_zom","BO_debu","BO_gman_kihon","BO_haride","BO_staje","BO_zonbiman","BO_kenkyu","BO_semu",
    "BO_kenkyu_c","BO_kenkyu_h","BO_kenkyu_m","BO_kenkyu_s","BO_lady_o","BO_mummy","BO_lady_l","BO_lady_m","BO_lady_n",
    "BO_neil","BO_siriru","BO_samson","BO_haris","BO_gilmoa","BO_bentry","BO_sophi","BO_hyum","BO_hyumhada","BO_dreik",
    "BO_kage","BO_lubin","BO_dkillerb","BO_neopetit","BO_neopetit2","BO_neopetit3","BO_pkenb","BO_bentryb","BO_pdolob",
    "BO_harisb","BO_taraba_b","BO_kenkyu_j","BO_devilon","BO_bentryc","BO_disiprin","BO_tetuman","BO_zonbi_b_2",
    "BO_devilonm","BO_neozom","BO_mummy2","BO_gilmoab","BO_moody","BO_kyurian","BO_mummyb","BO_mummy2b","BO_gilmoac",
    "BO_boss3","BO_bentryd","BO_pbaba","BO_zsophi","BO_kage2","BO_devilonx","BO_moodyb","BO_harisc","BO_bentrye",
    "BO_mummy2c","BO_gilmoad","BO_hiru_b","BO_boss4","BO_harisd","BO_kyurianb","BO_dkiller_jp","BO_dkiller_bl_jp",
    "BO_dkillerb_jp","BO_taalu","BO_taalub","BO_burner","BO_burnerb","BO_mummyc","BO_kage2b","BO_moodyc","BO_bentryf",
    "BO_tarabab","BO_gfrog","BO_moodybb","BO_moodycc"};
}

// ---- screen 6: Cheat (THOTD.EXE 0x4228CC; its tail is at 0x41FEF4) ----
// background 0xD1, "Cheat" (white 32) at y 0x40, then rows 0x5CE8D8 apart from y 0x40 + 0x20: a name (x 0x80, 0x180
// wide, white 24 left) and its value (same rect, right): Stage "%s : %d" (0x5A1320), Scene, Free-Play (label 0x25 On /
// 0x26 Off); Start (0x420F70) two rows on, Exit (0x24, 0x4210F4, Escape) two rows after it
void Frontend::screenCheat()
{
    screen_ = 6;
    freeTree();
    texts_.clear();
    texts_.reserve(8);
    Widget* root = widgetNew();
    root_ = root;
    setRect(root, 0, 0, 0, 0);
    setImage(root, 0xD1, -1, 0x20000A);
    Widget* title = widgetNew();
    addChild(root, title);
    setRect(title, 0, 0x40, 0, 0);
    setText(title, "Cheat", 0x10020);
    cheatStage_ = std::clamp(cheatStage_, 0, 3);
    if (cheatScene_ >= kPcScenes[cheatStage_])
        cheatScene_ = 0;
    int y = 0x40 + spacing_;
    auto row = [&](const char* name, const std::string& value, std::function<void()> act, int valueLabel) {
        Widget* n = widgetNew();
        addChild(root, n);
        setRect(n, 0x80, y, 0x180, spacing_);
        texts_.push_back(name);
        setText(n, texts_.back(), 0x20002);
        Widget* v = widgetNew();
        addChild(root, v);
        setRect(v, 0x80, y, 0x180, spacing_);
        if (valueLabel >= 0)
            setLabel(v, valueLabel, 0x20004);
        else {
            texts_.push_back(value);
            setText(v, texts_.back(), 0x20004);
        }
        setHandlers(v, [this, act](Widget&) {
            walking_ = false;
            act();
        }, &Frontend::hitRect);   // 0x428294
        y += spacing_;
    };
    char buf[32];
    std::snprintf(buf, sizeof buf, "%s : %d", kPcStage[cheatStage_], cheatStage_);
    row("Stage:", buf, [this]() {   // 0x422790: the next stage (wraps at the event lists' count 0xD5A978), scene 0
        cheatStage_ = (cheatStage_ + 1) % 4;
        cheatScene_ = 0;
        screenCheat();
    }, -1);
    std::snprintf(buf, sizeof buf, "%s : %d", kPcScenesOf[cheatStage_][cheatScene_], cheatScene_);
    row("Scene:", buf, [this]() {   // 0x4227CC: the next scene (wraps at the stage's event count 0xD5A920)
        cheatScene_ = (cheatScene_ + 1) % kPcScenes[cheatStage_];
        screenCheat();
    }, -1);
    row("Free-Play", "", [this]() {   // 0x4228C0: xor byte 0xD5B35C, 1
        freePlay = !freePlay;
        if (onFreePlay)
            onFreePlay(freePlay);
        screenCheat();
    }, freePlay ? 0x25 : 0x26);
    y += spacing_;
    Widget* start = widgetNew();
    addChild(root, start);
    setRect(start, 0, y, 0, 0);
    setText(start, "Start", 0x20020);
    setHandlers(start, [this](Widget&) {   // 0x420F70 (frontend_state 2; game_new 0x458588 reads 0xD5B350 / 0xD5B354)
        walking_ = false;
        if (onCheatStart) {
            const auto f = onCheatStart;
            f(cheatStage_, cheatScene_, freePlay);
        }
    }, &Frontend::hitText);
    y += 2 * spacing_;
    Widget* exit = widgetNew();
    addChild(root, exit);
    setRect(exit, 0, y, 0, 0);
    setLabel(exit, 0x24, 0x20020);
    setHandlers(exit, [this](Widget&) { walking_ = false; screenMainMenu(); }, &Frontend::hitText);   // 0x4210F4
    exit->tag = 1;
}

// ---- screen 5: Creature Test (THOTD.EXE screen_creature_test 0x422324) ----
// The original's view widget (0x60, 0x40, 0x1C4 wide; draw 0x422774 -> 0x41D598) shows the body object turning; ours
// shows the names and the yaw only (no 3D view in the front end yet). The MO line stays at motion 0: next / last
// MO's handlers are 0x42792C alone in THOTD.EXE.
void Frontend::screenCreatureTest()
{
    screen_ = 5;
    freeTree();
    texts_.clear();
    texts_.reserve(10);
    Widget* root = widgetNew();
    root_ = root;
    setRect(root, 0, 0, 0, 0);
    setImage(root, 0xD1, -1, 0x20000A);   // (the original: its 3D view; ours: the hall)
    auto text = [&](const std::string& s, int x, int y, int ww, uint32_t fl) {
        Widget* w = widgetNew();
        addChild(root, w);
        texts_.push_back(s);
        setText(w, texts_.back(), fl);
        setRect(w, x, y, ww, ww ? spacing_ : 0);
        return w;
    };
    text("Creature Test", 0, 0x20, 0, 0x10020);
    char buf[48];
    std::snprintf(buf, sizeof buf, "%d  %s", creatureBo_, kPcBodyNames[creatureBo_]);
    text(buf, 0, 0x80, 0, 0x20020);
    text("MO_b1_atk_a", 0, 0x80 + spacing_, 0, 0x20020);
    std::snprintf(buf, sizeof buf, "yaw %d", creatureYaw_);
    text(buf, 0, 0x80 + 2 * spacing_, 0, 0x20020);
    // ours: the 3D view through the Gallery (the original draws it in the view widget, 0x422774)
    const bool view = onCreatureView && (!creatureViewable || creatureViewable(creatureBo_));
    if (Widget* v = text("View in Gallery", 0, 0x80 + 3 * spacing_ + 8, 0, view ? 0x20020 : 0xA0020); view) {
        v->help = "Open this creature in the Gallery (3D, its motions)";
        setHandlers(v, [this](Widget&) {
            walking_ = false;
            const auto f = onCreatureView;
            f(creatureBo_);
        }, &Frontend::hitText);
    }
    auto button = [&](Widget* w, std::function<void()> act) {
        setHandlers(w, [this, act](Widget&) {
            walking_ = false;
            act();
            screenCreatureTest();
        }, &Frontend::hitText);   // 0x428178
    };
    const int y = 0x160;
    Widget* l = widgetNew();
    addChild(root, l);
    setRect(l, 0x60, y, 0xC4, spacing_);
    setLabel(l, 0x32, 0x20002);   // Left (0x422274: yaw 0x5CEBF8 += 0xB4)
    button(l, [this]() { creatureYaw_ += 0xB4; });
    Widget* r = widgetNew();
    addChild(root, r);
    setRect(r, 0x60, y, 0xC4, spacing_);
    setLabel(r, 0x33, 0x20004);   // Right (0x422290: -= 0xB4)
    button(r, [this]() { creatureYaw_ -= 0xB4; });
    button(text("last BO", 0x15C, y, 0xC4, 0x20002), [this]() { creatureBo_ = creatureBo_ > 0 ? creatureBo_ - 1 : 0x5D; });
    button(text("next BO", 0x15C, y, 0xC4, 0x20004), [this]() { creatureBo_ = creatureBo_ < 0x5D ? creatureBo_ + 1 : 0; });
    text("last MO", 0x15C, y + spacing_, 0xC4, 0x20002);   // (0x42792C only in the original)
    text("next MO", 0x15C, y + spacing_, 0xC4, 0x20004);
    Widget* exit = widgetNew();
    addChild(root, exit);
    setRect(exit, 0, y + 0x40, 0, 0);
    setLabel(exit, 0x24, 0x20020);
    setHandlers(exit, [this](Widget&) { walking_ = false; screenMainMenu(); }, &Frontend::hitText);   // 0x42223C
    exit->tag = 1;
}

// ---- screen 17 (ours): the Gallery (game/gallery.h draws the model into kGalleryRect) ----
void Frontend::drawHole(Widget& w)
{
    int x, y, ww, hh;
    absRect(w, x, y, ww, hh);
    for (int j = std::max(0, y); j < std::min(kHeight, y + hh); ++j)
        for (int i = std::max(0, x); i < std::min(cw_, x + ww); ++i)
            (*canvas_)[size_t(j * cw_ + i) * 4 + 3] = 0;
}

// ours: the Gallery's category column (screens 17 and 19), the shown category yellow; Model / Background / Exit are
// the gallery page's own (screenGallery adds them under it)
void Frontend::galleryColumn(Widget* root, const std::vector<std::string>& categories, int selected)
{
    static const char* const kHelp[] = {
        "The two agents and their second looks. Key: 1",
        "Sophie, Dr. Curien and the researchers to rescue. Key: 2",
        "Every creature the game sends at you, with the official names. Key: 3",
        "The four chapter bosses. Key: 4",
        "The golden frog, the items hidden in props, and props worth a look. Key: 5",
        "Creatures and models in the arcade data that the game never uses. Key: 6",
    };
    const int n = int(categories.size());
    for (int c = 0; c <= n; ++c) {
        const bool scenes = c == n;
        const std::string label = scenes ? "Cut-scenes" : categories[size_t(c)];
        Widget* w = widgetNew();
        addChild(root, w);
        texts_.push_back(label);
        setText(w, texts_.back(), 0x20000 | (c == selected ? kTextYellow : 0));
        setRect(w, 10, 50 + c * 25, 146, 24);
        w->fitW = 146;   // (sweep2: a long translation fitted to the column)
        if (lang::rtl())
            w->ttfScale = 0.85f;   // (sweep2: the Arabic font's rows touched)
        if (scenes) {
            w->help = "The players' cut-scenes, played by the game itself: 1-4 players, arcade or PC models. Key: C";
            if (onScenes)
                setHandlers(w, [this](Widget&) { walking_ = false; screenScenes(); }, &Frontend::hitText);
            else
                w->flags |= 0x80000;   // (grey: no Cut-scenes page)
        } else {
            w->help = c < int(sizeof kHelp / sizeof kHelp[0]) ? kHelp[c] : "";
            setHandlers(w, [this, c](Widget&) {
                walking_ = false;
                if (screen_ == 19) {   // from the Cut-scenes page: the Gallery at that category
                    if (openGallery) {
                        const auto g = openGallery;
                        g();
                    }
                }
                if (onGallery) {
                    const auto f = onGallery;
                    f(100 + c);
                }
                screenGallery();
            }, &Frontend::hitText);
        }
    }
}

// sweep2 (rtl): a page laid out for left to right turned round for a right-to-left language: each widget with a width
// moved to the other side of the 640-unit page, its text's alignment mirrored (left <-> right; centred as it is). The
// whole-width texts (width 0) stay. The Gallery's 3D window follows (galleryRect, galleryMirror_)
void Frontend::mirrorPage(Widget* root)
{
    for (Widget* w = root->child; w; w = w->next) {
        if (w->w <= 0)
            continue;
        w->x = kWidth - w->x - w->w;
        if (!(w->flags & 0x20) && (w->flags & 0x2000))
            w->flags = (w->flags & 0x04) ? (w->flags & ~0x04u) | 0x02u : (w->flags & ~0x02u) | 0x04u;
    }
}

// a text cut into lines of at most `width` characters at spaces (the 6 x 10 font's info lines)
static std::vector<std::string> wrapText(const std::string& s, size_t width)
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

void Frontend::screenGallery()
{
    castCd_ = -1;   // (galact: the Cut-scenes page's Actors list closed)
    galleryMirror_ = false;   // (sweep2: the page below laid out left to right, then mirrored for rtl at its end)
    if (style == kStyleRemake && galleryText) {   // regallery: the Remake style's own Gallery (its default layout, 0)
        const GalleryText g = galleryText();
        galleryRe_ = g.layout != 1 && g.layout != 2;
        if (galleryRe_) {
            galleryFull_ = g.full;
            galleryInfo_ = g.info;
            galleryRemake_ = false;
            remakeGallery(g);
            return;
        }
    }
    galleryRe_ = false;
    screen_ = 17;
    freeTree();
    texts_.clear();
    const GalleryText g = galleryText ? galleryText() : GalleryText{};
    Widget* root = widgetNew();
    root_ = root;
    setRect(root, 0, 0, 0, 0);
    setImage(root, 0xD1, -1, 0x20000A);   // the hall without the logo
    auto text = [&](const std::string& s, int x, int y, int w, int h, uint32_t flags) {
        Widget* t = widgetNew();
        addChild(root, t);
        setText(t, s, flags);
        setRect(t, x, y, w, h);
        return t;
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
    galleryFull_ = g.full;
    galleryInfo_ = g.info;
    galleryRemake_ = g.layout == 1 && !g.full;
    if (galleryRemake_) {   // gallery3: the optional Remake layout (gallery_remake.cpp)
        screenGalleryRemake(g, root);
        return;
    }
    if (g.full) {   // gallery3: the full view: the model over the whole window, a line of text at the bottom
        if (!g.status.empty())
            text(g.status, 0, kHeight / 2 - 6, kWidth, 12, 0x800020);
        text(g.group + "   " + std::to_string(g.index + 1) + " / " + std::to_string(g.count) +
                 (g.motion.empty() || !g.playing ? std::string() : "   Motion " + g.motion),
             0, 404, kWidth, 12, 0x800020);
        text(g.name, 0, 416, kWidth, 24, 0x20020);
        // the controls, small (the host fades this band out after a few seconds; a key brings it back)
        const char* l1 = g.pad ? "Right stick: turn   Left stick: pan   LT / RT: zoom   R3: reset   LB / RB: entry   L3: category"
                               : "Drag: turn   Right drag: pan   Wheel: zoom   R: reset   Q / E: entry   Tab: category";
        const std::string l2 = std::string(g.pad ? "X: motion " : "Space: motion ") + (g.playing ? "off" : "on") +
                               (g.pad ? "   Y: next motion   View: background   Start / B: back"
                                      : "   M: next motion   L: light   O: slow turn   B: background   T / Esc: back");
        text(l1, 0, kGalleryHintY + 4, kWidth, 12, 0x800020);
        text(l2, 0, kGalleryHintY + 15, kWidth, 12, 0x800020);
        Widget* hole = widgetNew();   // (added last: drawn first, under the texts)
        addChild(root, hole);
        setRect(hole, 0, 0, kWidth, kHeight);
        hole->draw = &Frontend::drawHole;
        hole->flags |= 0x100;
        return;
    }
    const int* r = galleryRect();
    if (classicBackdrop())   // remakepages: the Remake style: the page over the Remake backdrop (no hall, no side bars)
        root->draw = &Frontend::drawBackdropShade;
    text("Gallery", r[0], 0x06, r[2], 0, 0x10020);
    // the categories, then the page's own items
    std::vector<std::string> cats = g.categories;
    if (cats.empty())
        cats = {"Players", "Humans", "Creatures", "Bosses", "Items", "Cut Content"};
    galleryColumn(root, cats, g.category);
    const int y1 = 50 + int(cats.size() + 1) * 25 + 14;
    Widget* src = text("Model: " + g.source, 10, y1, 146, 24, g.otherSource ? 0x20000 : 0xA0000);
    src->help = g.otherSource ? "Switch between the arcade and the PC models. Key: S" : "Only one model source is there";
    if (g.otherSource)
        setHandlers(src, act(2), &Frontend::hitText);
    Widget* bg = text("Background", 10, y1 + 25, 146, 24, 0x20000);
    bg->help = "The window's background: dark, the game's skies, plain colours. Key: B, pad: View / Back";
    setHandlers(bg, act(7), &Frontend::hitText);
    text(g.background, 12, y1 + 49, 146, 12, 0x800000);
    Widget* reset = text("Reset view", 10, y1 + 66, 146, 24, 0x20000);
    reset->help = "Turn it back to the front, the first zoom, no pan. Keys: R, Home, pad: R3";
    setHandlers(reset, act(5), &Frontend::hitText);
    Widget* full = text("Full view", 10, y1 + 91, 146, 24, 0x20000);   // gallery3
    full->help = "The model over the whole screen, the page hidden. Key: T, pad: Start (Esc / B back)";
    setHandlers(full, act(11), &Frontend::hitText);
    Widget* info = text(g.info ? "Info: On" : "Info: Off", 10, y1 + 116, 146, 24, 0x20000);
    info->help = "Show or hide the info panel under the model (hidden: a bigger window). Key: I";
    setHandlers(info, act(12), &Frontend::hitText);
    Widget* lay = text("Layout: Classic (K)", 12, y1 + 143, 146, 12, 0x800000);
    lay->help = "The Gallery's other look, after the 2022 Remake's gallery: portraits, a room behind the model. Key: K";
    setHandlers(lay, act(15), &Frontend::hitText);
    Widget* exit = text("Exit", 10, 400, 146, 24, 0x20000);
    exit->help = "Back to the main menu (Escape)";
    exit->tag = 1;   // Escape
    setHandlers(exit, act(6), &Frontend::hitText);
    // the info panel under the window
    if (!g.status.empty())
        text(g.status, r[0], r[1] + r[3] / 2 - 6, r[2], 12, 0x800020);
    text(g.group + "  " + std::to_string(g.index + 1) + " / " + std::to_string(g.count), r[0] + r[2] - 200, r[1] - 13, 200, 12,
         0x800004);
    if (g.info) {
        text(g.name, r[0], r[1] + r[3] + 3, r[2], 24, 0x20020);
        int y = r[1] + r[3] + 30;
        auto line = [&](const std::string& s, bool raw) {
            if (s.empty()) return;
            text(s, r[0], y, r[2], 12, 0x800020)->raw = raw;
            y += 12;
        };
        line(g.where.empty() ? std::string() : T("Appears:") + " " + T(g.where), true);
        for (const std::string& l : wrapTr(g.facts, 66, wrapText)) line(l, true);   // (lang: translated, then cut)
        for (const std::string& l : wrapTr(g.text, 66, wrapText)) line(l, true);
    } else {
        text(g.name, r[0], r[1] - 13, 260, 12, 0x800000);   // (no info panel: the name small at the top left)
    }
    if (!g.motion.empty())
        text("Motion " + g.motion, r[0], 386, r[2], 12, 0x800020);
    struct Button { std::string label, help; int action; };
    const Button row[4] = {
        {"< Prev", "The one before in this category. Keys: Q, Page Up, pad: LB", 0},
        {"Motion", "Its next motion (or damage step). Key: M (Shift+M back), pad: Y", 3},
        {g.playing ? "Hold" : "Play", "Play the motion, or stop it on this frame (off at first). Key: Space, pad: X", 4},
        {"Next >", "The next one in this category. Keys: E, Page Down, pad: RB", 1},
    };
    for (int c = 0; c < 4; ++c) {
        Widget* w = text(row[c].label, r[0] + c * r[2] / 4, 402, r[2] / 4, 24, 0x20020);
        w->help = row[c].help;
        setHandlers(w, act(row[c].action), &Frontend::hitText);
    }
    text(g.pad ? "LB/RB: entry  L3: category  sticks: turn, pan  LT/RT: zoom  X: motion  Start: hide UI"
               : "Q/E: entry  Tab: category  drag: turn  right drag: pan  wheel: zoom  Space: motion  T: hide UI",
         0, 434, 0, 12, 0x800020);
    // the 3D window (added last: drawn first, under the texts)
    Widget* hole = widgetNew();
    addChild(root, hole);
    setRect(hole, r[0], r[1], r[2], r[3]);
    hole->draw = &Frontend::drawHole;
    hole->flags |= 0x100;
    hole->help = "Drag to turn it, right / middle drag to pan, the wheel zooms; R or Home: the first view, T: hide the page";
    if (lang::rtl()) {   // sweep2: the categories at the right, the window at the left (galleryRect follows)
        for (Widget* w = root->child; w; w = w->next)
            if (w->w == 146 && w->h == 24)   // (the side column's rows: the Arabic font's touched)
                w->ttfScale = 0.85f, w->fitW = 146;
        mirrorPage(root);
        galleryMirror_ = true;
    }
}

// ---- screen 19 (ours): the Gallery's Cut-scenes page (game/scene_view.h) ----
void Frontend::screenScenes()
{
    // testing (galact): HOTD_SCENES_LIST=<role 0-3>: the page's first build opens that Actors row's list (both styles;
    // with HOTD_GALLERY_SHOT / HOTD_GALLERY_KEYS: its picture)
    static bool listTried = false;
    if (static const char* tl = std::getenv("HOTD_SCENES_LIST"); tl && !listTried) {
        listTried = true;
        castCd_ = std::clamp(std::atoi(tl), 0, 3), castCdScroll_ = -1;
        ddRow_ = castCd_, ddKey_ = "scenes", ddScroll_ = -1, ddFocus_ = true;
    }
    if (style == kStyleRemake && galleryText) {   // regallery: in the Remake Gallery's language (its layout 0)
        const GalleryText g = galleryText();
        galleryRe_ = g.layout != 1 && g.layout != 2;
        if (galleryRe_) {
            remakeScenes(scenesText ? scenesText() : ScenesText{}, g);
            return;
        }
    }
    galleryRe_ = false;
    screen_ = 19;
    freeTree();
    texts_.clear();
    const ScenesText s = scenesText ? scenesText() : ScenesText{};
    Widget* root = widgetNew();
    root_ = root;
    setRect(root, 0, 0, 0, 0);
    setImage(root, 0xD1, -1, 0x20000A);   // the hall without the logo
    if (classicBackdrop())   // remakepages: over the Remake backdrop (as the Gallery's page)
        root->draw = &Frontend::drawBackdropShade;
    auto text = [&](const std::string& t, int x, int y, int w, int h, uint32_t flags) {
        Widget* wd = widgetNew();
        addChild(root, wd);
        setText(wd, t, flags);
        setRect(wd, x, y, w, h);
        return wd;
    };
    auto act = [this](int a) {
        return [this, a](Widget&) {
            walking_ = false;
            castCd_ = -1;   // (galact: any action closes the Actors list)
            if (onScenes) {
                const auto f = onScenes;
                f(a);
            }
            if (a == 3)   // Exit: the main menu (gallery2: the Cut-scenes page is one of the Gallery's categories)
                screenMainMenu();
            else if (screen_ == 19)   // (Play leaves the menu)
                screenScenes();
        };
    };
    // gallery2: the Gallery's category column on the left (this page is its last item), the page right of it
    const int* gr = kGalleryRect;
    const int x0 = gr[0], pw = gr[2];
    text("Cut-scenes", x0, 0x06, pw, 0, 0x10020);
    text("The game's own scene code plays them", x0, 0x2E, pw, 12, 0x800020);
    {
        const GalleryText g = galleryText ? galleryText() : GalleryText{};
        std::vector<std::string> cats = g.categories;
        if (cats.empty())
            cats = {"Players", "Humans", "Creatures", "Bosses", "Items", "Cut Content"};
        galleryColumn(root, cats, int(cats.size()));
    }
    const int half = (int(s.rows.size()) + 1) / 2;
    for (int i = 0; i < int(s.rows.size()); ++i) {
        const int col = i / std::max(half, 1), row = i % std::max(half, 1);
        const bool sel = i == s.selected;
        Widget* w = text((sel ? "> " : "  ") + s.rows[size_t(i)], x0 + col * (pw / 2), 0x46 + row * 23, pw / 2, 22,
                         0x20000 | (sel ? kTextYellow : 0));
        w->fitW = pw / 2 - 8;   // (sweep2: a long translation kept off the next column)
        if (lang::rtl())
            w->ttfScale = 0.8f;   // (sweep2: the Arabic font's rows overlapped)
        w->help = sel ? "The chosen scene: Play shows it" : "Choose this scene";
        setHandlers(w, act(100 + i), &Frontend::hitText);
    }
    const int y1 = 0x46 + half * 23 + 6;
    text(s.line, x0, y1, pw, 12, 0x800020);
    struct Button { std::string label, help; int action; bool on; };
    const Button rows[3] = {
        {"Players: " + std::to_string(s.players), "1 to 4 players in the game; 3 or 4: the 4P mode. Key: P", 0, true},
        {std::string("Models: ") + (s.pc ? "PC" : "Arcade"), s.pcThere ? "The arcade's or the 1998 PC release's models. Key: M"
                                                                        : "Only the arcade models are there", 1, s.pcThere},
        {"Play", "Watch it. The game runs ahead to it first, the next time at once. Key: F5", 2, true},
    };
    for (int c = 0; c < 3; ++c) {
        Widget* w = text(rows[c].label, x0 + c * pw / 3, y1 + 18, pw / 3, 24, rows[c].on ? 0x20020 : 0xA0020);
        w->fitW = pw / 3 - 6;
        if (lang::rtl())
            w->ttfScale = 0.85f;
        w->help = rows[c].help;
        if (rows[c].on)
            setHandlers(w, act(rows[c].action), &Frontend::hitText);
    }
    // Clean View (for recording clips): no HUD, crosshairs, cursor or viewer text while watching
    Widget* cw = text(std::string("Clean View: ") + (s.clean ? "On" : "Off"), x0, y1 + 44, pw / 3, 24, 0x20020);
    cw->help = "On: only the picture while watching (no HUD, crosshairs or cursor; the keys show for 2 s). Key: C";
    setHandlers(cw, act(4), &Frontend::hitText);
    // trailer (user 2026-10-21): the cut-scenes' captions on / off (with Clean View: a fully clean picture)
    Widget* sw = text(std::string("Subtitles: ") + (s.subtitles ? "On" : "Off"), x0 + pw / 3, y1 + 44, pw / 3, 24, 0x20020);
    sw->help = "The cut-scenes' captions. Off with Clean View On: only the picture, for recording. Key: T";
    setHandlers(sw, act(5), &Frontend::hitText);
    // liveattract (user 2026-10-21): the arcade's attract as it is, full screen with its sound (any key / button: back
    // here); the host starts it (attractAsked)
    Widget* ad = text("Attract Demo", x0 + 2 * pw / 3, y1 + 44, pw / 3, 24, 0x20020);
    ad->help = "The arcade's attract (title, story, demo play) full screen with sound. Any key or button: back";
    setHandlers(ad, [this](Widget&) {
        walking_ = false;
        attractAsked = true;
    }, &Frontend::hitText);
    for (Widget* w : {cw, sw, ad}) {   // (sweep2: as the row above)
        w->fitW = pw / 3 - 6;
        if (lang::rtl())
            w->ttfScale = 0.85f;
    }
    // actorswap: Actors, a column per role (its name, who plays it; a click or keys 1-4: the next, Shift: back)
    const int nCast = int(std::min(s.castRoles.size(), s.castActors.size()));
    for (int r = 0; r < nCast; ++r) {
        const int cx = x0 + r * pw / std::max(nCast, 1), cwid = pw / std::max(nCast, 1);
        for (int k = 0; k < 2; ++k) {
            Widget* w = text(k == 0 ? s.castRoles[size_t(r)] : s.castActors[size_t(r)], cx, y1 + 69 + k * 11, cwid, 12,
                             0x800020 | (k == 1 ? kTextYellow : 0));
            w->fitW = cwid - 6;   // (sweep2: the columns' names apart; rtl: the two lines too)
            if (lang::rtl())
                w->ttfScale = 0.85f;
            if (s.castChoices.empty()) {
                w->help = "Who plays this part while you watch (the picture only). Click or keys 1-4 (Shift: back)";
                setHandlers(w, act(10 + r), &Frontend::hitText);
            } else {   // galact: a click opens the list of every actor (keys 1-4 still step)
                w->help = "Who plays this part while you watch (the picture only). Click: the list; keys 1-4 (Shift: back)";
                setHandlers(w, [this, r](Widget&) {
                    walking_ = false;
                    castCd_ = castCd_ == r ? -1 : r;
                    castCdScroll_ = -1;
                    screenScenes();
                }, &Frontend::hitText);
            }
        }
    }
    const int y2 = nCast > 0 ? y1 + 30 : y1;   // (the lines below move down under the Actors)
    if (!s.status.empty())
        text(s.status, x0, y2 + 66 + (nCast > 0 ? 0 : 6), pw, 12, 0x800020);
    // (cutfix: slow motion on M, Free Cam on F, a pad's stick click)
    text("Space pause, R replay, M slow, F free cam, C clean, T subs, A actors, Esc back", 0,
         y2 + 80 + (nCast > 0 ? 0 : 6), 0, 12, 0x800020);
    text("Q / E: scene  P: players  M: models  1-4: actors  C: clean  T: subs  F5: play  Tab: category", 0, 434, 0, 12, 0x800020);
    Widget* back = text("Exit", 10, 400, 146, 24, 0x20000);
    back->help = "Back to the main menu (Escape)";
    back->tag = 1;   // Escape
    setHandlers(back, act(3), &Frontend::hitText);
    const bool mirrored = lang::rtl();   // sweep2: the categories at the right, the first scene column right of the second
    if (mirrored)
        mirrorPage(root);
    // galact (user 2026-10-28: "I think a list could be added here"): an Actors column's list (a click on it): every
    // actor in a dark box under it (as the settings pages' Classic dropdown), the role's pick yellow, a long list
    // scrolled ("more" lines, the bar); a click picks, a click outside / Escape closes it (Frontend::input)
    if (castCd_ < 0 || castCd_ >= nCast || s.castChoices.empty() || int(s.castNow.size()) <= castCd_)
        return;
    const int role = castCd_;
    const int nItems = int(s.castChoices.size()), curK = s.castNow[size_t(role)];
    const int ih = 22, m = 4, moreH = 12;
    int widest = 0;
    for (const std::string& c : s.castChoices)
        widest = std::max(widest, rowTextWidth(c));
    const int cwid = pw / std::max(nCast, 1), colT = y1 + 66;   // (the list above the column: the room is there)
    const int colX = mirrored ? kWidth - (x0 + role * cwid) - cwid : x0 + role * cwid;
    int vis = std::min(nItems, 12);
    auto heightOf = [&](int v) { return v * ih + 2 * m + (v < nItems ? 2 * moreH : 0); };
    while (vis > 3 && heightOf(vis) > colT - 6)
        --vis;
    const bool scroll = vis < nItems;
    const int lh = heightOf(vis), sbW = scroll ? 12 : 0;
    const int lw = std::min(kWidth - 8, widest + 24 + sbW);
    const int lx = std::clamp(colX, 4, kWidth - 4 - lw), ly = std::max(4, colT - lh - 2);
    if (castCdScroll_ < 0)
        castCdScroll_ = curK - vis / 2;
    castCdScroll_ = std::clamp(castCdScroll_, 0, nItems - vis);
    auto append = [this](Widget* w) {   // (at the root's end: drawn over everything; the catcher last)
        Widget** tail = &root_->child;
        while (*tail)
            tail = &(*tail)->next;
        *tail = w;
    };
    Widget* box = widgetNew();
    append(box);
    setRect(box, lx, ly, lw, lh);
    box->draw = &Frontend::drawCBox;
    for (int v = 0; v < vis; ++v) {
        const int k = castCdScroll_ + v;
        Widget* it = widgetNew();
        append(it);
        setRect(it, lx + 10, ly + m + (scroll ? moreH : 0) + v * ih, lw - 14 - sbW, ih);
        texts_.push_back(s.castChoices[size_t(k)]);
        setText(it, texts_.back(), 0x20002);
        it->help = "Who plays " + s.castRoles[size_t(role)] + " while you watch";
        if (k == curK)
            it->draw = &Frontend::drawTextLit;
        setHandlers(it, act(-(1000 + 100 * role + k)), &Frontend::hitRect);
    }
    for (int d = 0; d < 2 && scroll; ++d) {   // the "more" lines (a click: the next / previous actors)
        if (d == 0 ? castCdScroll_ == 0 : castCdScroll_ + vis >= nItems)
            continue;
        Widget* mo = widgetNew();
        append(mo);
        setRect(mo, lx, d ? ly + lh - m - moreH : ly + m, lw - sbW, moreH);
        setText(mo, d ? "more below" : "more above", 0x800020);
        setHandlers(mo, [this, d, vis](Widget&) {
            walking_ = false;
            castCdScroll_ += d ? vis - 1 : 1 - vis;
            screenScenes();
        }, &Frontend::hitRect);
    }
    if (scroll)
        scrollBar(root_, lx + lw - sbW, ly + m + moreH, 6, vis * ih, castCdScroll_, vis, nItems, [this](int first) {
            castCdScroll_ = first;
            screenScenes();
        }, true);
    Widget* catcher = widgetNew();   // (outside the list: closes it)
    append(catcher);
    setRect(catcher, 0, 0, cw_, kHeight);
    setHandlers(catcher, [this](Widget&) {
        walking_ = false;
        castCd_ = -1;
        screenScenes();
    }, &Frontend::hitRect);
}

// ---- screen 9 (ours): Chapter Select, the PC's boss-select portraits for the chapters' bosses ----
void Frontend::screenChapters()
{
    if (style == kStyleRemake) {   // remakepages: the Remake layout (frontend_remake_pages.cpp)
        remakeChapters();
        return;
    }
    screen_ = 9;
    freeTree();
    texts_.clear();
    texts_.reserve(12);
    Widget* root = widgetNew();
    root_ = root;
    setRect(root, 0, 0, 0, 0);
    setImage(root, 0xD1, -1, 0x20000A);   // the hall without the logo
    Widget* title = widgetNew();
    addChild(root, title);
    setText(title, "Chapter Select", 0x10020);
    setRect(title, 0, 0x20, 0, 0);
    static const char* const kBoss[4] = {"The Chariot", "The Hanged Man", "The Hermit", "The Magician"};
    const int colW = 148, x0 = (cw_ - 4 * colW) / 2;
    for (int c = 0; c < 4; ++c) {
        const bool on = chapterEnabled[c] && onChapter;
        auto go = [this, c](Widget&) {
            walking_ = false;
            if (onChapter) {   // playerpick: the players first (Back: Chapter Select)
                const auto f = onChapter;
                askPlayers("Chapter " + std::to_string(c + 1), [f, c]() { f(c); }, [this]() { screenChapters(); });
            }
        };
        const std::string help = on ? "Start Chapter " + std::to_string(c + 1) + " (its boss: " + kBoss[c] + ")"
                                    : "Chapter " + std::to_string(c + 1) + ": not playable yet";
        Widget* col = widgetNew();
        addChild(root, col);
        setRect(col, x0 + c * colW, 0x60, colW, 0xC0);
        Widget* pic = widgetNew();
        addChild(col, pic);
        setRect(pic, (colW - 120) / 2, 0, 120, 120);
        setImage(pic, 0xE3 + c, on ? 0xE8 + c : -1, 0x20);
        pic->help = help;
        if (on)
            setHandlers(pic, go, &Frontend::hitRect);
        Widget* l = widgetNew();
        addChild(col, l);
        setRect(l, 0, 124, colW, 24);
        texts_.push_back("Chapter " + std::to_string(c + 1));
        setText(l, texts_.back(), on ? 0x20020 : 0xA0020);
        l->help = help;
        if (on)
            setHandlers(l, go, &Frontend::hitText);
        Widget* b = widgetNew();
        addChild(col, b);
        setRect(b, 0, 152, colW, 12);
        setText(b, kBoss[c], 0x800020);
    }
    // the ending: no start of its own (it plays after Chapter 4's Magician)
    Widget* end = widgetNew();
    addChild(root, end);
    setRect(end, 0, 0x130, 0, 24);
    setText(end, "The Ending", chapterEnabled[4] ? 0x20020 : 0xA0020);
    end->help = chapterEnabled[4] ? "The ending plays after the Magician: clear Chapter 4 to see it" : "The ending: not ported yet";
    Widget* endNote = widgetNew();
    addChild(root, endNote);
    setRect(endNote, 0, 0x14C, 0, 12);
    setText(endNote, chapterEnabled[4] ? "after Chapter 4" : "not ported yet", 0x800020);
    Widget* ex = widgetNew();
    addChild(root, ex);
    setRect(ex, 0x40, 0x1A0, 0x200, 24);
    setLabel(ex, 0x24, 0x20020);
    ex->help = "Back to the main menu (Escape)";
    setHandlers(ex, [this](Widget&) {
        walking_ = false;
        screenMainMenu();
    }, &Frontend::hitText);
    ex->tag = 1;
}

// ---- 0x423038 screen_high_scores (screen 8) ----
void Frontend::screenRanking()
{
    if (style == kStyleRemake) {   // remakepages
        remakeRanking();
        return;
    }
    screen_ = 8;
    freeTree();
    texts_.clear();
    texts_.reserve(2);
    Widget* root = widgetNew();
    root_ = root;
    setRect(root, 0, 0, 0, 0);
    setImage(root, 0xD2, -1, 0x20000A);   // the pistol on the desk
    Widget* title = widgetNew();
    addChild(root, title);
    setRect(title, 0x40, 0x10, 0x200, 0);
    setLabel(title, 0x43, 0x10020);   // "High Scores", white 32, centred
    const int nTables = std::max(1, int(rankingTables.size()));
    rankingTable = std::clamp(rankingTable, 0, nTables - 1);
    Widget* table = widgetNew();
    addChild(root, table);
    setRect(table, 0x40, spacing_ + 0x10, 0x200, 0);
    if (rankingTable == 0 || rankingTables.empty())
        setLabel(table, 0x44, 0x20020);   // "Arcade Mode" (the table shown; 0x5CE320)
    else {
        texts_.push_back(rankingTables[size_t(rankingTable)]);
        setText(table, texts_.back(), 0x20020);
    }
    table->ttfScale = 0.8f;   // (sweep2: the language's font touched the title above)
    // the original's page arrows (0xD8 / 0xD9, yellow 0xDA / 0xDB while hovered: 0x424E10 / 0x424D64 turned its boss
    // tables); ours turn our tables
    if (nTables > 1)
        for (int d = 0; d < 2; ++d) {
            Widget* a = widgetNew();
            addChild(root, a);
            setRect(a, d ? 0x240 - 32 : 0x40, spacing_ + 0x10, 32, 24);
            setImage(a, d ? 0xD9 : 0xD8, d ? 0xDB : 0xDA, 0);
            a->help = d ? "The next table" : "The table before";
            setHandlers(a, [this, d, nTables](Widget&) {
                walking_ = false;
                rankingTable = (rankingTable + (d ? 1 : nTables - 1)) % nTables;
                screenRanking();
            }, &Frontend::hitRect);
        }
    Widget* ex = widgetNew();
    addChild(root, ex);
    setRect(ex, 0x40, 0x1A0, 0x200, 0);
    setLabel(ex, 0x24, 0x20020);   // "Exit"
    ex->help = "Back to the main menu (Escape)";
    setHandlers(ex, [this](Widget&) {   // 0x4210F4: the main menu
        walking_ = false;
        screenMainMenu();
    }, &Frontend::hitText);
    ex->tag = 1;   // 0x427CB0: Escape
    const std::vector<RankRow> rows = rankingTableRows ? rankingTableRows(rankingTable)
                                      : rankingRows ? rankingRows() : std::vector<RankRow>();
    if (!rows.empty() && rows[0].ch >= 0) {   // PC Mode's table (screen 9): its own four columns
        pcModeRankColumns(root, rows);
        return;
    }
    if (!rows.empty() && rows[0].time) {   // a boss's times (screens 10-14 0x423A90 ..): name and time, two columns
        for (int c = 0; c < 2; ++c) {
            Widget* col = widgetNew();
            addChild(root, col);
            setRect(col, c == 0 ? 0x40 : 0x160, 0x60, 0xE0, 400);
            for (int i = 0; i < 10; ++i) {
                Widget* cell = widgetNew();
                addChild(col, cell);
                setRect(cell, 0, i * 0x1A, 0xE0, 0x1A);
                const RankRow r = size_t(i) < rows.size() ? rows[size_t(i)] : RankRow{};
                setText(cell, c == 0 ? r.name : r.stage, c == 0 ? 0x20002u : 0x20004u);
            }
        }
        return;
    }
    // three columns, ten rows each (0x423288 / 0x423344 / 0x423429). uxpolish: the arcade's tables keep no stage
    // (every row "-"): the stage column is left empty then instead of a column of dashes
    if (rows.empty()) {   // (uxpolish: an empty table said nothing at all)
        Widget* none = widgetNew();
        addChild(root, none);
        setRect(none, 0, 0xC0, 0, 24);
        setText(none, "No scores yet", 0xA0020);
        return;
    }
    bool stages = false;
    for (const RankRow& r : rows)
        stages = stages || (!r.stage.empty() && r.stage != "-");
    const int colX[3] = {0x40, 0x100, 0x1C0};
    for (int c = 0; c < 3; ++c) {
        Widget* col = widgetNew();
        addChild(root, col);
        setRect(col, colX[c], 0x60, 0x80, 0x190);
        for (int i = 0; i < 10; ++i) {
            Widget* cell = widgetNew();
            addChild(col, cell);
            setRect(cell, 0, i * 0x1A, 0x80, 0x1A);
            const RankRow r = size_t(i) < rows.size() ? rows[size_t(i)] : RankRow{};
            const std::string s = c == 0 ? r.name : c == 1 ? (stages ? r.stage : std::string())
                                                           : (size_t(i) < rows.size() ? std::to_string(r.score) : std::string());
            setText(cell, s, c == 0 ? 0x20002 : 0x20004);   // the name left-aligned, the stage and the score right
        }
    }
}

// ---- Boss Mode (THOTD.EXE 0x422B90 screen 7, 0x423A90.. screens 10-14, the arrows 0x424D64 / 0x424E10) ----
void Frontend::screenBossMode()
{
    if (style == kStyleRemake) {   // remakepages
        remakeBossMode();
        return;
    }
    screen_ = 7;
    freeTree();
    Widget* root = widgetNew();
    root_ = root;
    setRect(root, 0, 0, 0, 0);
    setImage(root, 0xA8, -1, 0x20000A);
    Widget* title = widgetNew();
    addChild(root, title);
    setRect(title, 0, 0x10, 0, 0);
    setLabel(title, 0x42, 0x10020);   // "Boss Mode", white 32, centred
    for (int b = 0; b < 5; ++b) {     // 0xE3.. (0x427E28: the sprite's size)
        const int id = 0xE3 + b;
        const int w = spr_.width(id), h = spr_.height(id);
        static const int kX[5] = {0x10, -1, 0x10, -1, -2}, kY[5] = {0x10, 0x10, -1, -1, -2};
        const int x = kX[b] == -1 ? 0x270 - w : kX[b] == -2 ? (0x280 - w) / 2 : kX[b];
        const int y = kY[b] == -1 ? 0x1D0 - h : kY[b] == -2 ? (0x1E0 - h) / 2 : kY[b];
        Widget* btn = widgetNew();
        addChild(root, btn);
        setRect(btn, x, y, w, h);
        setImage(btn, id, id + 5, 0x100060);
        // uxpolish: the PC's pictures have no names: the help line says who it is and what the run is
        static const char* const kHelp[5] = {
            "The Chariot (Chapter 1's boss): beat it as fast as you can, the best times go in its table",
            "The Hanged Man (Chapter 2's boss): beat it as fast as you can, the best times go in its table",
            "The Hermit (Chapter 3's boss): beat it as fast as you can, the best times go in its table",
            "The Magician (Chapter 4's boss): beat it as fast as you can, the best times go in its table",
            "All Bosses: the four bosses one after the other on one clock"};
        btn->help = kHelp[b];
        setHandlers(btn, [this, b](Widget&) {   // 0x422EE4 ..: boss mode 2-6, frontend_state 4
            walking_ = false;
            if (onBossMode) {   // playerpick: the players first (Back: Boss Mode)
                const auto f = onBossMode;
                askPlayers(label(0x45 + b), [f, b]() { f(b + 2); }, [this]() { screenBossMode(); });
            }
        }, &Frontend::hitRect);
    }
    Widget* ex = widgetNew();
    addChild(root, ex);
    setRect(ex, 0, 0x1A0, 0, 0);
    setLabel(ex, 0x24, 0x20020);   // "Exit"
    ex->help = "Back to the main menu (Escape)";
    setHandlers(ex, [this](Widget&) {   // 0x422EC8: the main menu
        walking_ = false;
        screenMainMenu();
    }, &Frontend::hitText);
    ex->tag = 1;
}

void Frontend::addBossArrows(Widget* root, int mode)
{
    auto step = [this, mode](int dir) {   // the next / last boss table (2 .. 6, round)
        walking_ = false;
        screenBossTimes(2 + (mode - 2 + (dir > 0 ? 1 : 4)) % 5);
    };
    Widget* r = widgetNew();
    addChild(root, r);
    setRect(r, 0x40, 0x1A0, 0x200, 0);
    setImage(r, 0xD9, 0xDB, 0x100004);
    r->help = "The next boss's times";
    setHandlers(r, [step](Widget&) { step(1); }, &Frontend::hitImage);
    Widget* l = widgetNew();
    addChild(root, l);
    setRect(l, 0x40, 0x1A0, 0x200, 0);
    setImage(l, 0xD8, 0xDA, 0x100002);
    l->help = "The previous boss's times";
    setHandlers(l, [step](Widget&) { step(-1); }, &Frontend::hitImage);
}

// ---- 0x424EA8 screen_15: the name entry (keys 0x425188, the field 0x424F7C) ----
void Frontend::screenNameEntry(const std::string& scoreText, int player)
{
    screen_ = 15;
    freeTree();
    name_.clear();   // (0x4252F9: the buffer emptied from 0x5A1378 after each entry)
    nameCursor_ = 0;
    Widget* root = widgetNew();
    root_ = root;
    setRect(root, 0, 0, 0, 0);
    setImage(root, 0xD2, -1, 0x20000A);
    const struct { int y, id; uint32_t flags; } kRows[] = {
        {0x20, player == 0 ? 0x4D : 0x4E, 0x20020}, {0x40, 0x4A, 0x10020}, {0x70, 0x4B, 0x10020}, {0xD0, 0x4C, 0x20020}};
    for (const auto& r : kRows) {
        Widget* t = widgetNew();
        addChild(root, t);
        setRect(t, 0, r.y, 0, 0);
        setLabel(t, r.id, r.flags);
    }
    Widget* score = widgetNew();
    addChild(root, score);
    setRect(score, 0, 0xA0, 0, 0);
    setText(score, scoreText, 0x10020);   // (score_time_format 0x5A1344 "%02d:%02d:%02d" for the times)
    Widget* field = widgetNew();
    addChild(root, field);
    setRect(field, 0, 0xF0, 0, 0);
    field->draw = &Frontend::drawNameField;
    // ours: the grid for the pointer (mouse, pads, light guns): four rows from y 0x128, 0x1C apart
    static const char* const kGrid[3] = {"ABCDEFGHIJ", "KLMNOPQRST", "UVWXYZ0123"};
    auto key = [this](Widget* w, int k) { setHandlers(w, [this, k](Widget&) { nameKey(k); }, &Frontend::hitText); };
    for (int r = 0; r < 4; ++r) {
        const int n = r < 3 ? 10 : 9;
        for (int c = 0; c < n; ++c) {
            Widget* b = widgetNew();
            addChild(root, b);
            const bool wide = r == 3 && c >= 6;
            setRect(b, wide ? 0x16C + (c - 6) * 0x40 : 0x7C + c * 0x28, 0x128 + r * 0x1C, wide ? 0x40 : 0x28, 0x1C);
            // (uxpolish: each cell's help line; "End" read like the End key: "Done")
            if (r < 3) {
                setText(b, std::string(1, kGrid[r][c]), 0x20020);
                key(b, kGrid[r][c]);
                b->help = "Add this letter (10 at most)";
            } else if (c < 6) {
                setText(b, std::string(1, char('4' + c)), 0x20020);
                key(b, '4' + c);
                b->help = "Add this letter (10 at most)";
            } else if (c == 6) {
                setText(b, "Spc", 0x20020);
                key(b, ' ');
                b->help = "Add a space";
            } else if (c == 7) {
                setText(b, "Del", 0x20020);
                key(b, kKeyBack);
                b->help = "Delete the letter before the cursor (Backspace)";
            } else {
                setText(b, "Done", 0x20020);
                setHandlers(b, [this](Widget&) { nameDone(); }, &Frontend::hitText);
                b->tag = 1;   // (Escape ends the entry, as the PC's 0x425208)
                b->help = "Done: put the name in the table (no name: ANONYMOUS; Enter)";
            }
        }
    }
}

void Frontend::nameKey(int k)
{
    const int len = int(name_.size());
    nameCursor_ = std::clamp(nameCursor_, 0, len);
    switch (k) {
    case kKeyEnter: nameDone(); return;
    case kKeyLeft: nameCursor_ = std::max(0, nameCursor_ - 1); return;      // 0x4253B9 (DIK 0xCB / 0x4B)
    case kKeyRight: nameCursor_ = std::min(len, nameCursor_ + 1); return;   // 0x4253EA (0xCD / 0x4D)
    case kKeyHome: nameCursor_ = 0; return;                                 // 0xC7
    case kKeyEnd: nameCursor_ = len; return;                                // 0xCF
    case kKeyDel:                                                           // 0xD3: the character at the cursor
        if (nameCursor_ < len) name_.erase(size_t(nameCursor_), 1);
        return;
    case kKeyBack:                                                          // 0x0E: the one before it
        if (nameCursor_ > 0) name_.erase(size_t(--nameCursor_), 1);
        return;
    default: break;
    }
    if (len >= 10 || !((k >= 'A' && k <= 'Z') || (k >= '0' && k <= '9') || k == ' '))   // 0x425503 (10), the key list 0x5CED21
        return;
    name_.insert(size_t(nameCursor_++), 1, char(k));
}

void Frontend::nameDone()
{
    walking_ = false;
    if (name_.empty())
        name_ = "ANONYMOUS";   // 0x42522D: 0x5A14C8
    const std::string n = name_;
    if (onNameEntered)
        onNameEntered(n);   // (0x425798 files the row; the table's screen follows: 0x42533C)
}

void Frontend::drawNameField(Widget& w)
{
    int x, y, ww, hh;
    absRect(w, x, y, ww, hh);
    constexpr uint32_t kFont = 0x20000;
    int total = 0;   // 0x424FEF: the name's width (a space or a missing glyph: 8)
    for (const char ch : name_) {
        const int g = ch == ' ' ? 0 : glyph(uint8_t(ch), kFont);
        total += g ? spr_.width(g) : 8;
    }
    x += (ww - total) / 2;
    for (int i = 0; i <= int(name_.size()); ++i) {
        if (i == nameCursor_ && blink_)   // 0x42507C: the cursor 0x15A before the character
            blit(0x15A, x, y);
        if (i == int(name_.size()))
            break;
        const int g = name_[size_t(i)] == ' ' ? 0 : glyph(uint8_t(name_[size_t(i)]), kFont);
        if (!g) {
            x += 8;
            continue;
        }
        blit(g, x, y);
        x += spr_.width(g);
    }
}

void Frontend::screenBossTimes(int mode, int highlight)
{
    mode = std::clamp(mode, 2, 6);
    if (style == kStyleRemake) {   // remakepages
        remakeBossTimes(mode, highlight);
        return;
    }
    screen_ = 30 + mode;   // (the PC's 10-14; ours 32-36)
    freeTree();
    Widget* root = widgetNew();
    root_ = root;
    setRect(root, 0, 0, 0, 0);
    setImage(root, 0xD2, -1, 0x20000A);
    Widget* title = widgetNew();
    addChild(root, title);
    setRect(title, 0x40, 0x10, 0x200, 0);
    setLabel(title, 0x43, 0x10020);   // "High Scores"
    Widget* name = widgetNew();
    addChild(root, name);
    setRect(name, 0x40, spacing_ + 0x10, 0x200, 0);
    setLabel(name, 0x45 + mode - 2, 0x20020);   // "Chariot Boss" .. "All Bosses"
    addBossArrows(root, mode);
    Widget* ex = widgetNew();
    addChild(root, ex);
    setRect(ex, 0x40, 0x1A0, 0x200, 0);
    setLabel(ex, 0x24, 0x20020);
    ex->help = "Back to the main menu (Escape)";
    setHandlers(ex, [this](Widget&) { walking_ = false; screenMainMenu(); }, &Frontend::hitText);
    ex->tag = 1;
    const std::vector<RankRow> rows = bossRows ? bossRows(mode) : std::vector<RankRow>();
    for (int c = 0; c < 2; ++c) {
        Widget* col = widgetNew();
        addChild(root, col);
        setRect(col, c == 0 ? 0x40 : 0x160, 0x60, 0xE0, 400);
        for (int i = 0; i < 10; ++i) {
            Widget* cell = widgetNew();
            addChild(col, cell);
            setRect(cell, 0, i * 0x1A, 0xE0, 0x1A);
            const RankRow r = size_t(i) < rows.size() ? rows[size_t(i)] : RankRow{};
            // (ours: the new row in the yellow font)
            setText(cell, c == 0 ? r.name : r.stage, (c == 0 ? 0x20002u : 0x20004u) | (i == highlight ? 0x40000u : 0u));
        }
    }
}

// ---- the sliders (0x426D5C / 0x426DC0 / 0x426E80): 16-pixel cells: the left arrow, 8 cells (the knob at the value),
// the right arrow; sprites by table 0x5CED60 {normal, highlighted}: left 0xDC / 0xDD, right 0xDE / 0xDF, bar 0xE0, knob
// 0xE1 / 0xE2 (highlighted while hovered: the arrow under the pointer, the knob elsewhere on the bar) ----
namespace {
const int kSliderSprites[4][2] = {{0xDC, 0xDD}, {0xDE, 0xDF}, {0xE0, 0xE0}, {0xE1, 0xE2}};
int sliderValue(const Frontend::Options& o, int which)
{
    const int v = which == 0 ? o.sfxVolume : o.musicVolume;
    return std::clamp((v * 7 + 50) / 100, 0, 7);
}
}

bool Frontend::hitSlider(Widget& w, int x, int y)
{
    int wx, wy, ww, wh;
    absRect(w, wx, wy, ww, wh);
    return x >= wx && x <= wx + 0xA0 && y >= wy && y <= wy + 0x18;
}

void Frontend::clickSlider(Widget& w)
{
    int wx, wy, ww, wh;
    absRect(w, wx, wy, ww, wh);
    const int cell = (px_ - wx) / 16;
    int v = sliderValue(options, w.user);
    if (cell == 0)
        v = std::max(v - 1, 0);
    else if (cell == 9)
        v = std::min(v + 1, 7);
    else
        v = cell - 1;
    // ours: 0-100 % in steps of 10 (the launcher's); the 8 steps map to 0, 14 .. 100 rounded to 10
    const int pct = std::clamp((v * 100 / 7 + 5) / 10 * 10, 0, 100);
    (w.user == 0 ? options.sfxVolume : options.musicVolume) = pct;
    if (onOptions)
        onOptions();
}

void Frontend::drawSlider(Widget& w)
{
    int x, y, ww, wh;
    absRect(w, x, y, ww, wh);
    const int cell = (px_ - x) / 16;
    const bool hov = (w.flags & 0x40000) != 0;
    const int v = sliderValue(options, w.user);
    blit(kSliderSprites[0][hov && cell == 0], x, y);
    x += 16;
    for (int i = 0; i < 8; ++i, x += 16) {
        if (i == v)
            blit(kSliderSprites[3][hov && cell != 0 && cell != 9], x, y);
        else
            blit(kSliderSprites[2][0], x, y);
    }
    blit(kSliderSprites[1][hov && cell == 9], x, y);
}

// ---- remakemenu: Options' values and steps (the PC handlers' cycles; both styles) ----
void Frontend::optionsStep(int which, int dir)
{
    auto wrap = [dir](int v, int n) { return ((v + dir) % n + n) % n; };
    switch (which) {
    case kOptDifficulty: options.difficulty = wrap(std::clamp(options.difficulty, 0, 4), 5); break;
    case kOptLives: options.lives = wrap(std::clamp(options.lives, 0, 11), 12); break;
    case kOptContinues: options.continues = wrap(std::clamp(options.continues, 0, 9), 10); break;   // (0x421564: +1, past 9 -> 0)
    case kOptAutoReload: options.autoReload = !options.autoReload; break;
    case kOptBlood: options.blood = wrap(std::clamp(options.blood, 0, 4), 5); break;   // (a custom colour 5 -> Red)
    case kOptSfx: case kOptMusic: {   // (the Audio page's 5 % steps, 0 after 100 %)
        int& v = which == kOptSfx ? options.sfxVolume : options.musicVolume;
        v = dir > 0 ? (v >= 100 ? 0 : v / 5 * 5 + 5) : (v <= 0 ? 100 : (v + 4) / 5 * 5 - 5);
        break;
    }
    default: break;
    }
}

std::string Frontend::optionsValue(int which) const
{
    static const int kBlood[6] = {0x1D, 0x1C, 0x1F, 0x20, 0x1E, 0x21};
    switch (which) {
    case kOptDifficulty: return label(0x17 + std::clamp(options.difficulty, 0, 4));
    case kOptLives: return std::to_string(kLivesTable[std::clamp(options.lives, 0, 11)]);
    case kOptContinues: return std::to_string(std::clamp(options.continues, 0, 9));
    case kOptAutoReload: return label(options.autoReload ? 0x25 : 0x26);
    case kOptBlood: return label(kBlood[std::clamp(options.blood, 0, 5)]);
    case kOptSfx: return std::to_string(options.sfxVolume) + "%";
    case kOptMusic: return std::to_string(options.musicVolume) + "%";
    default: return "";
    }
}

// the Remake style's "Game" tab: Options' rows (the same labels, values, helps and steps as screen 2)
std::vector<Frontend::Extra> Frontend::optionsRows()
{
    static const int kLabel[kOptRows] = {0xC, 0xD, 0xE, 0x11, 0x12, 0x15, 0x16};
    static const char* const kHelp[kOptRows] = {
        "How hard the game is: Very Easy .. Very Hard (the arcade's operator levels)",
        "The lives each player starts with (1P and 2P together)",
        "Continues in PC Mode (0-9); arcade games are free play",
        "An empty gun reloads by itself (not in the originals; 1P and 2P together)",
        "The blood's colour (a custom colour: Gameplay)",
        "The sound effects' volume",
        "The music's volume (more audio settings: Audio)"};
    std::vector<Extra> r;
    for (int k = 0; k < kOptRows; ++k) {
        Extra e;
        e.name = label(kLabel[k]);
        e.value = [this, k]() { return optionsValue(k); };
        e.click = [this, k]() {
            optionsStep(k, 1);
            if (onOptions) onOptions();
        };
        e.help = kHelp[k];
        e.clickBack = [this, k]() {
            optionsStep(k, -1);
            if (onOptions) onOptions();
        };
        // remakefix: the short choices' values (the Remake style's dropdown): Difficulty, Blood Color
        if (k == kOptDifficulty)
            for (int i = 0; i < 5; ++i) e.choices.push_back(label(0x17 + i));
        if (k == kOptBlood)
            for (const int id : {0x1D, 0x1C, 0x1F, 0x20, 0x1E, 0x21}) e.choices.push_back(label(id));
        // optui (user 2026-10-26): Lives (its 12 settings: the start lives and the most, launcher.cpp's table),
        // Continues 0-9 and the volumes in steps of 5 % as lists too, set at once
        if (k == kOptLives) {
            static const int kMost[12] = {3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 5};
            for (int i = 0; i < 12; ++i)
                e.choices.push_back(std::to_string(kLivesTable[i]) + " (up to " + std::to_string(kMost[i]) + ")");
            e.choiceNow = [this]() { return std::clamp(options.lives, 0, 11); };
        }
        if (k == kOptContinues) {
            for (int i = 0; i < 10; ++i) e.choices.push_back(std::to_string(i));
            e.choiceNow = [this]() { return std::clamp(options.continues, 0, 9); };
        }
        if (k == kOptSfx || k == kOptMusic) {
            for (int i = 0; i <= 20; ++i) e.choices.push_back(std::to_string(i * 5) + "%");
            e.choiceNow = [this, k]() {
                const int v = k == kOptSfx ? options.sfxVolume : options.musicVolume;
                return v % 5 == 0 ? v / 5 : -1;
            };
        }
        if (e.choiceNow)
            e.pickAt = [this, k](int i) {
                if (k == kOptLives) options.lives = i;
                else if (k == kOptContinues) options.continues = i;
                else (k == kOptSfx ? options.sfxVolume : options.musicVolume) = i * 5;
                if (onOptions) onOptions();
            };
        r.push_back(std::move(e));
        // relmenu (user 2026-10-30: Free Play "good but add into the game settings"): Gameplay's Free Play (the Cheat
        // screen's Free-Play, launcher.ini freeplay=) here too, right under Continues (one setting: onFreePlay saves it)
        if (k == kOptContinues) {
            Extra fp;
            fp.name = "Free Play";
            fp.value = [this]() { return label(freePlay ? 0x25 : 0x26); };
            fp.click = [this]() {
                freePlay = !freePlay;
                if (onFreePlay) onFreePlay(freePlay);
            };
            fp.clickBack = fp.click;
            fp.help = "On: PC Mode's continues never run out (as the 1998 PC's Cheat screen). Arcade games are always free play";
            r.push_back(std::move(fp));   // (an On / Off switch as Auto Reload: a click flips it)
        }
    }
    Extra cc;
    cc.name = label(0x14);   // Controller Config (0x421484; ours: the Controls page)
    cc.value = []() { return std::string("Open >"); };
    cc.click = [this]() {
        if (onControllerConfig) {
            const auto f = onControllerConfig;
            f();
        }
    };
    cc.help = "Open the Controls page: each player's input, controllers, keys and buttons, light guns";
    r.push_back(std::move(cc));
    return r;
}

// ---- screen 2: Options (0x41E428) ----
void Frontend::screenOptions()
{
    if (style == kStyleRemake) {   // remakemenu: the "Game" tab of the settings pages
        optRows_ = optionsRows();
        remakeSettings(label(0xB), &optRows_, 0, [this]() { screenMainMenu(); });
        return;
    }
    screen_ = 2;
    freeTree();
    texts_.clear();
    texts_.reserve(8);
    Widget* root = widgetNew();
    root_ = root;
    setRect(root, 0, 0, 0, 0);
    setImage(root, 0xA7, -1, 0x20000A);   // 0xA9 when 0xC66EE4 (the in-game options) is set
    Widget* title = widgetNew();
    addChild(root, title);
    setLabel(title, 0xB, 0x10020);        // "Options", white 32, centred
    setRect(title, 0, 0x20, 0, 0);
    Widget* panel = widgetNew();
    addChild(root, panel);
    setRect(panel, 0x60, 0x60, 0x1C0, 0);
    int tw, th;
    measure("A", 0x20000, tw, th);
    const int col1 = panel->w - 0x98, col2 = panel->w - 0x48, colW = 0x48;
    auto changed = [this](int screen) {
        return [this, screen](Widget&) {
            if (onOptions)
                onOptions();
            walking_ = false;   // 0x42792C
            if (screen == 2)
                screenOptions();
        };
    };
    int y = 0;
    auto row = [&](int labelId, const char* help) {
        Widget* l = widgetNew();
        addChild(panel, l);
        setRect(l, 0, y, 0, th);
        setLabel(l, labelId, 0x20002);
        l->help = help;   // ours: the help line
        return l;
    };
    auto valueLabel = [&](Widget* parent, int id, std::function<void(Widget&)> act, bool locked = false) {
        Widget* v = widgetNew();
        addChild(parent, v);
        setLabel(v, id, locked ? 0xA0004 : 0x20004);
        if (act)
            setHandlers(v, std::move(act), &Frontend::hitRect);
        return v;
    };
    auto valueText = [&](Widget* parent, const std::string& t, std::function<void(Widget&)> act, bool locked = false) {
        Widget* v = widgetNew();
        addChild(parent, v);
        texts_.push_back(t);
        setText(v, texts_.back(), locked ? 0xA0004 : 0x20004);
        if (act)
            setHandlers(v, std::move(act), &Frontend::hitRect);
        return v;
    };
    // a 1P / 2P pair (Life, Auto Reload): the value right-aligned in its 72-pixel cell, "1P" / "2P" left in the same
    auto pair = [&](int col, int labelId, std::function<void(Widget*)> fill) {
        Widget* v = widgetNew();
        addChild(panel, v);
        setRect(v, col, y, colW, th);
        fill(v);
        int lw = 0, lh = 0;   // leftovers: a translated value fits beside "1P" / "2P" (Arabic / Hebrew Off: smaller)
        measure(label(labelId), 0x20002, lw, lh);
        v->fitW = std::max(8, colW - lw - 12);
        Widget* l = widgetNew();
        addChild(panel, l);
        setRect(l, col, y, colW, 0);
        setLabel(l, labelId, 0x20002);
    };
    // Difficulty (ours: the five operator levels, labels 0x17 Very Easy .. 0x1B Very Hard)
    {
        Widget* l = row(0xC, "How hard the game is: Very Easy .. Very Hard (the arcade's operator levels)");
        valueLabel(l, 0x17 + std::clamp(options.difficulty, 0, 4), [this, changed](Widget& w) {
            optionsStep(kOptDifficulty, 1);
            changed(2)(w);
        });
    }
    y += spacing_;
    // Life 1P / 2P
    row(0xD, "The lives every player starts with: one setting for all, 3P / 4P of the 4 players mode too");
    for (int p = 0; p < 2; ++p)
        pair(p ? col2 : col1, p ? 0x35 : 0x34, [&](Widget* v) {
            texts_.push_back(std::to_string(kLivesTable[std::clamp(options.lives, 0, 11)]));
            setText(v, texts_.back(), 0x20004);
            setHandlers(v, [this, changed](Widget& w) {
                optionsStep(kOptLives, 1);
                changed(2)(w);
            }, &Frontend::hitRect);
        });
    y += spacing_;
    // Continues (THOTD.EXE 0xD5B1E0, text table 0x5CEC0C "0".."9", handler 0x421564: +1, past 9 -> 0; default 5):
    // PC Mode's continues (pc_continues_init 0x4488E4); arcade games play free play as the arcade
    // relmenu (user 2026-10-30: Free Play "add into the game settings"; the PC's screen has no room for a row of its
    // own): after 9 the value is Free Play (Gameplay > Free Play, the Cheat screen's Free-Play), then 0 again
    // ("Free": the picture's box is two digits wide; a translation is fitted to it)
    valueText(row(0xE, "Continues in PC Mode (0-9, then Free Play: they never run out); arcade games are free play"),
              freePlay ? std::string("Free") : std::to_string(std::clamp(options.continues, 0, 9)),
              [this, changed](Widget& w) {
                  if (freePlay || options.continues >= 9) {
                      freePlay = !freePlay;
                      if (!freePlay)
                          options.continues = 0;
                      if (onOptions)
                          onOptions();   // (first: onFreePlay's save copies the settings back into `options`)
                      if (onFreePlay)
                          onFreePlay(freePlay);
                  } else
                      optionsStep(kOptContinues, 1);
                  changed(2)(w);
              })->fitW = 30;
    y += spacing_;
    // Auto Reload 1P / 2P
    row(0x11, "An empty gun reloads by itself (not in the originals): one setting for all, 3P / 4P too");
    for (int p = 0; p < 2; ++p)
        pair(p ? col2 : col1, p ? 0x35 : 0x34, [&](Widget* v) {
            setLabel(v, options.autoReload ? 0x25 : 0x26, 0x20004);
            setHandlers(v, [this, changed](Widget& w) {
                optionsStep(kOptAutoReload, 1);
                changed(2)(w);
            }, &Frontend::hitRect);
        });
    y += spacing_;
    // Blood Color: 0 Red, 1 Green, 2 Blue, 3 Purple, 4 Yellow (the original's order and labels; ours the same values;
    // our custom colour (5) is kept by the text launcher and shown as None here)
    {
        static const int kBlood[6] = {0x1D, 0x1C, 0x1F, 0x20, 0x1E, 0x21};
        valueLabel(row(0x12, "The blood's colour (a custom colour: Gameplay)"), kBlood[std::clamp(options.blood, 0, 5)], [this, changed](Widget& w) {
            optionsStep(kOptBlood, 1);
            changed(2)(w);
        });
    }
    y += spacing_;
    // Sound Effects / Music sliders
    for (int k = 0; k < 2; ++k) {
        row(k ? 0x16 : 0x15, k ? "The music's volume (more audio settings: Audio)" : "The sound effects' volume");
        Widget* sl = widgetNew();
        addChild(panel, sl);
        setRect(sl, panel->w - 10 * spr_.width(0xE0), y, 0, 0);
        sl->draw = &Frontend::drawSlider;
        sl->user = k;
        setHandlers(sl, [this](Widget& w) { clickSlider(w); }, &Frontend::hitSlider);
        y += spacing_;
    }
    // Resolution (ours, user 2026-10-21: the Video page's Resolution, its page of sizes; without the host's hooks grey
    // "Auto": the picture is scaled to any window)
    if (onResolution)
        valueText(row(0x5D, "Fullscreen: Auto (the desktop's size) or a display mode; in a window: its size (F11 switches)"),
                  resolutionValue ? resolutionValue() : std::string("Auto"), [this](Widget&) {
                      walking_ = false;
                      if (onResolution)
                          onResolution();
                  });
    else
        valueText(row(0x5D, "The picture is scaled to any window (F11: fullscreen; Video: Aspect)"), "Auto", nullptr, true);
    y += spacing_;
    // Controller Config (0x421484: the controls screen; ours: the Controls page)
    {
        Widget* l = row(0x14, "Open the Controls page: each player's input, controllers, keys and buttons, light guns");
        setHandlers(l, [this](Widget&) {
            walking_ = false;
            if (onControllerConfig)
                onControllerConfig();
        }, &Frontend::hitText);
    }
    y += spacing_;
    // ReWrithe: our settings' categories (Video, Audio, ...) as a strip across the screen, in the gap the original
    // leaves before Exit (the "More Options" row before; without categories: that row)
    if (!tabs.empty())
        tabStrip(root, panel->y + y, -1);
    else if (!extras.empty()) {
        Widget* m = widgetNew();
        addChild(panel, m);
        setRect(m, 0, y, 0, th);
        setText(m, "More Options", 0x20002);
        setHandlers(m, [this](Widget&) {
            walking_ = false;
            screenExtras();
        }, &Frontend::hitText);
    }
    y += spacing_;
    // Exit (0x421450), tag 1: Escape
    Widget* ex = widgetNew();
    addChild(panel, ex);
    setRect(ex, 0, y, 0, 0);
    setLabel(ex, 0x24, 0x20020);
    ex->help = "Back to the main menu (Escape)";
    setHandlers(ex, [this](Widget&) {
        walking_ = false;
        screenMainMenu();
    }, &Frontend::hitText);
    ex->tag = 1;
}

// ---- ReWrithe's page (screen 100; ours, in the Options screen's style) ----
void Frontend::screenExtras()
{
    if (style == kStyleRemake) {   // remakemenu: the same rows on the Remake style's page
        remakeSettings(extrasTitle, &extras, extrasTab >= 0 ? extrasTab + 1 : -1, [this]() {
            if (extrasExit) {   // a copy: it may clear itself
                const auto back = extrasExit;
                back();
            } else
                screenOptions();
        });
        return;
    }
    screen_ = 100;
    freeTree();
    Widget* root = widgetNew();
    root_ = root;
    setRect(root, 0, 0, 0, 0);
    setImage(root, 0xD1, -1, 0x20000A);   // the hall without the logo (the Sophie pictures have the Options boxes drawn in)
    Widget* title = widgetNew();
    addChild(root, title);
    setText(title, extrasTitle, 0x10020);
    setRect(title, 0, 0x14, 0, 0);
    // a category's page: the strip of the categories under the title
    if (extrasTab >= 0)
        tabStrip(root, 0x3A, extrasTab);
    Widget* panel = widgetNew();
    addChild(root, panel);
    setRect(panel, 0x60, 0x5C, 0x1C0, 0);
    int tw, th;
    measure("A", 0x20000, tw, th);
    // pages of kExtrasPerPage rows at the High Scores table's 0x1A step; Exit (with the PC's page arrows beside it when
    // there are more pages) sits at a fixed place under them, the help line under that: nothing ever leaves the
    // 640 x 480 screen (the picture is scaled as a whole to any window)
    if (extrasTitle != extrasPageTitle_) {   // another page set (Steam): from its first page
        if (!extrasPageTitle_.empty())
            extrasPage = 0;
        extrasPageTitle_ = extrasTitle;
    }
    const int per = kExtrasPerPage, n = int(extras.size());
    const int pages = std::max(1, (n + per - 1) / per);
    extrasPage = std::clamp(extrasPage, 0, pages - 1);
    const int step = 0x1A;
    int y = 0;
    // optui: the dropdown open (a right-click on a row with a list): the rows, Exit and the page arrows wait
    if (cdRow_ >= 0 && (cdKey_ != extrasTitle || cdRow_ < extrasPage * per || cdRow_ >= std::min(n, (extrasPage + 1) * per)))
        cdRow_ = -1;
    const bool cd = cdRow_ >= 0;
    if (cd)
        cdKey_ = extrasTitle;
    cRows_.clear();
    for (int i = extrasPage * per; i < std::min(n, (extrasPage + 1) * per); ++i, y += step) {
        const Extra& e = extras[i];
        Widget* l = widgetNew();
        addChild(panel, l);
        setRect(l, 0, y, 0, th);
        cRows_.push_back({panel->x, panel->y + y, panel->w, th, i});
        std::string v = e.value ? e.value() : std::string();
        // uxpolish: a row that does nothing now is grey with the reason as its help line; an experimental one has the
        // small tag after its label and "Experimental:" before its help line
        const std::string off = e.off ? e.off() : std::string();
        const bool on = e.click && off.empty();
        const bool exp = e.experimental && e.experimental();
        setText(l, e.name, !on ? 0xA0002 : 0x20002);
        l->raw = e.raw;
        l->rtlRow = true;   // (rtl: the label at the right, the value at the left)
        l->help = !off.empty() ? off : e.help;
        if (off.empty() && exp && e.help.rfind("Experimental", 0) != 0) {
            std::string h = e.help;   // "Experimental: four players ..." (a capital kept before another one: "PC")
            if (h.size() > 1 && h[0] >= 'A' && h[0] <= 'Z' && h[1] >= 'a' && h[1] <= 'z') h[0] = char(h[0] - 'A' + 'a');
            l->help = "Experimental: " + h;
        }
        // the value fitted beside the label (cut with "..." when it would run into it; the tag only where it fits)
        const int labelW = rowTextWidth(e.name);
        // (soak2: the value translated before it is cut: a cut English text ("Host / Join...") has no translation,
        // so the Arabic Co-op page showed English; the value is then drawn as it is)
        const bool vRaw = e.raw || langOn();
        if (langOn() && !e.raw)
            v = lang::tr(v);
        if (!v.empty() && labelW > 0) {
            auto valueW = [this](const std::string& s) { int w2 = 0, h2 = 0; measureRaw(s, 0x20000, w2, h2); return w2; };
            // (misc17: a whole letter at a time, and it ends: before, when not even "..." fitted beside a long label
            // (Hebrew, Japanese), "..." / "....." / "...." went round for ever and the menu froze)
            if (v.size() > 1)
                v = util::utf8Fit(v, "...", [&](const std::string& t) { return labelW + 16 + valueW(t) <= panel->w; });
        }
        if (exp && labelW > 0)
            experimentalTag(panel, labelW + 8, y + 8, panel->w - (v.empty() ? 0 : 16 + rowTextWidth(v)));
        auto act = [this, i](Widget&) {
            if (extras[i].click) {   // a copy: the click may replace the rows (the Steam page)
                const auto click = extras[i].click;
                click();
            }
            walking_ = false;
            if (screen_ == 100)
                screenExtras();   // the new value
        };
        // uxpolish: the whole row is the button (was: the value only, the label did nothing), lit together on hover
        if (on && !cd) {
            setHandlers(l, act, &Frontend::hitRect);
            l->litKids = true;
        }
        if (e.swatch && on && !v.empty()) {   // menufb: the colour square left of the value
            Widget* sw = widgetNew();
            addChild(root, sw);
            setRect(sw, panel->x + panel->w - rowTextWidth(v) - 12 - 26, panel->y + y + 2, 26, th - 4);
            sw->tint = e.swatch();
            sw->draw = &Frontend::drawSwatch;
        }
        if (e.barGet && on && !cd && !v.empty()) {   // menufb: a value bar left of the value
            const int vw = std::max(rowTextWidth(v), rowTextWidth("100%")), bw2 = 120;
            sliderBar(root, panel->x + panel->w - vw - 14 - bw2, panel->y + y, bw2, th, e.barGet(), e.barMax, e.barStep, [this, i](int nv) {
                if (extras[size_t(i)].barSet) {
                    const auto set = extras[size_t(i)].barSet;
                    set(nv);
                }
                walking_ = false;
                if (screen_ == 100)
                    screenExtras();
            });
        }
        if (v.empty())   // an action row: the label itself
            continue;
        Widget* val = widgetNew();
        addChild(l, val);
        setText(val, v, !on ? 0xA0004 : 0x20004);
        val->raw = vRaw;
        val->rtlRow = true;
    }
    const int exitY = per * step + 6;   // panel-relative: 0x5C + 0x13E = 0x19A
    if (pages > 1) {   // the PC's arrows (0xD8 / 0xD9, yellow while hovered) turn the pages; "n/m" between them
        for (int d = 0; d < 2; ++d) {
            Widget* a = widgetNew();
            addChild(panel, a);
            setRect(a, d ? panel->w - 32 : 0, exitY, 32, 24);
            setImage(a, d ? 0xD9 : 0xD8, d ? 0xDB : 0xDA, 0);
            a->help = d ? "The next page of these settings" : "The page before";
            if (!cd)
                setHandlers(a, [this, d, pages](Widget&) {
                    walking_ = false;
                    extrasPage = (extrasPage + (d ? 1 : pages - 1)) % pages;
                    screenExtras();
                }, &Frontend::hitRect);
        }
        Widget* pg = widgetNew();
        addChild(panel, pg);
        setRect(pg, 40, exitY, 80, 24);
        setText(pg, std::to_string(extrasPage + 1) + "/" + std::to_string(pages), 0xA0002);
    }
    Widget* ex = widgetNew();
    addChild(panel, ex);
    setRect(ex, 0, exitY, 0, 24);
    setLabel(ex, 0x24, 0x20020);
    ex->help = extrasExit ? "Back to the previous page (Escape)" : "Back to Options (Escape)";
    if (!cd)
        setHandlers(ex, [this](Widget&) {
            walking_ = false;
            if (extrasExit) {   // a copy: it may clear itself
                const auto back = extrasExit;
                back();
            }
            else
                screenOptions();
        }, &Frontend::hitText);
    ex->tag = 1;
    // the rows' sight pictures (the dropdowns' icons; menufb: the floating preview became the Firing Range's page)
    artRows_ = &extras;
    artSig_.clear();
    for (int i = 0; i < n; ++i)
        if (extras[i].art)
            artSig_ += extras[i].value ? extras[i].value() + "|" : std::string("|");
    if (extrasRange() && rangeSig)
        artSig_ += "#" + rangeSig();
    // menufb (user 2026-10-26: "no indication that the list is scrollable"): the pages' scroll bar at the rows' right
    // (its thumb: this page's rows among all of them; dragged / clicked: that page)
    if (pages > 1 && !cd)
        scrollBar(root, panel->x + panel->w + 10, panel->y, 6, per * step - 6, extrasPage * per, per, n, [this, per, pages](int first) {
            extrasPage = std::clamp((first + per / 2) / per, 0, pages - 1);
            screenExtras();
        });
    // menufb: the Firing Range under the rows (its page: one page of rows)
    if (extrasRange() && pages == 1) {
        const int top = panel->y + n * step + 4, bottom = panel->y + exitY - 8;
        if (bottom - top >= 70)
            rangeWidgets(root, panel->x - 16, top, panel->w + 32, bottom - top);
    }
    if (!cd)
        return;
    // optui: the dropdown (a right-click on a row with a list; the click still steps): the values in a dark box under
    // the row (above it without room), the current one yellow, a long list scrolled (the wheel, its "more" lines);
    // a click outside closes it
    const Extra& e = extras[size_t(cdRow_)];
    const RowRect* rr = nullptr;
    for (const RowRect& q : cRows_)
        if (q.i == cdRow_) rr = &q;
    if (!rr)
        return;
    const std::vector<std::string> ch = e.choices;
    const int nItems = int(ch.size()), curIdx = e.choiceNow ? e.choiceNow() : -1;
    const std::string curV = e.value ? e.value() : std::string();
    const bool icons = bool(e.art);
    const int ih = icons ? 30 : th + 4, m = 4, moreH = 12;
    int widest = 0;
    for (const std::string& c : ch)
        widest = std::max(widest, rowTextWidth(c));
    const int lw0 = widest + 24 + (icons ? 36 : 0);
    const int below = kHeight - 4 - (rr->y + rr->h + 2), above = rr->y - 2 - 4;
    int vis = std::min(nItems, icons ? 8 : 12);
    auto heightOf = [&](int v) { return v * ih + 2 * m + (v < nItems ? 2 * moreH : 0); };
    while (vis > 3 && heightOf(vis) > std::max(below, above))
        --vis;
    const bool scroll = vis < nItems;
    const int lh = heightOf(vis), sbW = scroll ? 12 : 0;   // (menufb: the scroll bar's room)
    const int lw = std::min(kWidth - 8, lw0 + sbW);
    const int lx = std::max(4, rr->x + rr->w - lw);
    int ly = rr->y + rr->h + 2;
    if (ly + lh > kHeight - 4)
        ly = std::max(4, rr->y - lh - 2);
    int curK = -1;
    for (int k = 0; k < nItems; ++k)
        if (curIdx >= 0 ? k == curIdx : ch[size_t(k)] == curV)
            curK = k;
    if (cdScroll_ < 0)
        cdScroll_ = curK >= 0 ? curK - vis / 2 : 0;
    cdScroll_ = std::clamp(cdScroll_, 0, nItems - vis);
    // (appended at the root's end: drawn over everything, tried first by nothing before them; the catcher last)
    auto append = [this](Widget* w) {
        Widget** tail = &root_->child;
        while (*tail)
            tail = &(*tail)->next;
        *tail = w;
    };
    Widget* box = widgetNew();
    append(box);
    setRect(box, lx, ly, lw, lh);
    box->draw = &Frontend::drawCBox;
    for (int v = 0; v < vis; ++v) {
        const int k = cdScroll_ + v;
        Widget* it = widgetNew();
        append(it);
        setRect(it, lx + 10 + (icons ? 36 : 0), ly + m + (scroll ? moreH : 0) + v * ih, lw - 14 - sbW - (icons ? 36 : 0), ih);
        texts_.push_back(ch[size_t(k)]);
        setText(it, texts_.back(), 0x20002);
        it->help = e.help;
        if (k == curK)
            it->draw = &Frontend::drawTextLit;
        else if (e.choiceOff && e.choiceOff(k))
            it->flags |= 0x80000;   // (padfix: not available now: grey)
        if (icons) {
            Widget* ic = widgetNew();
            addChild(it, ic);
            setRect(ic, -36, 0, 34, ih);
            ic->id = int16_t(k);
            ic->user = cdRow_;
            ic->draw = &Frontend::drawCItemIcon;
        }
        setHandlers(it, [this, i = cdRow_, k](Widget&) {
            walking_ = false;
            pickChoice(extras, i, k);
            cdClose();
        }, &Frontend::hitRect);
    }
    for (int d = 0; d < 2 && scroll; ++d) {   // the "more" lines (a click: the next / previous values)
        if (d == 0 ? cdScroll_ == 0 : cdScroll_ + vis >= nItems)
            continue;
        Widget* mo = widgetNew();
        append(mo);
        setRect(mo, lx, d ? ly + lh - m - moreH : ly + m, lw - sbW, moreH);
        setText(mo, d ? "more below" : "more above", 0x800020);
        mo->help = e.help;
        setHandlers(mo, [this, d, vis](Widget&) {
            walking_ = false;
            cdScroll_ += d ? vis - 1 : 1 - vis;
            screenExtras();
        }, &Frontend::hitRect);
    }
    if (scroll)   // menufb: the list's scroll bar at its right
        scrollBar(root_, lx + lw - sbW, ly + m + moreH, 6, vis * ih, cdScroll_, vis, nItems, [this](int first) {
            cdScroll_ = first;
            screenExtras();
        }, true);
    Widget* catcher = widgetNew();   // (outside the list: closes it)
    append(catcher);
    setRect(catcher, 0, 0, cw_, kHeight);
    setHandlers(catcher, [this](Widget&) {
        walking_ = false;
        cdClose();
    }, &Frontend::hitRect);
}

// optui: a list's value k picked (both styles): set at once (pickAt / pick), else the row's own steps to it
void Frontend::pickChoice(std::vector<Extra>& rows, int i, int k)
{
    if (i < 0 || i >= int(rows.size()))
        return;
    const Extra& e = rows[size_t(i)];
    if (k < 0 || k >= int(e.choices.size()) || (e.choiceOff && e.choiceOff(k)))
        return;   // (padfix: a grey choice is not picked)
    const std::string want = e.choices[size_t(k)];
    const auto value = e.value;
    const auto click = e.click;
    const int nItems = int(e.choices.size());
    if (const auto at = e.pickAt)
        at(k);        // (a row that sets its value by index: Lives, Frame Rate ...)
    else if (const auto pick = e.pick)
        pick(want);   // (a row that sets a value at once: Display)
    else
        for (int g = 0; g < nItems && value && click && value() != want; ++g)
            click();   // (the row's own steps: its save and hooks as a click)
}

void Frontend::openList(int row)
{
    if (row < 0 || row >= int(extras.size()))
        return;
    if (style == kStyleRemake) {
        remakeScroll_ = std::max(0, row - 8);
        ddRow_ = row, ddKey_ = remakeKey_, ddScroll_ = -1, ddFocus_ = true;
        if (rebuild_) {
            const auto f = rebuild_;
            f();
        }
        return;
    }
    extrasPage = row / kExtrasPerPage;
    cdRow_ = row, cdKey_ = extrasTitle, cdScroll_ = -1;
    screenExtras();
}

void Frontend::cdClose()
{
    if (cdRow_ < 0)
        return;
    cdRow_ = -1;
    walking_ = false;
    if (screen_ == 100 && sc_ == 1)
        screenExtras();
}

// optui: the Classic dropdown's box: a dark fill, a grey line round it
void Frontend::drawCBox(Widget& w)
{
    int x, y, ww, hh;
    absRect(w, x, y, ww, hh);
    std::vector<uint8_t>& c = *canvas_;
    const int W = cw_ * sc_, H = int(c.size() / 4) / std::max(1, W);
    for (int py = std::max(0, y); py < std::min(H, y + hh); ++py)
        for (int px = std::max(0, x); px < std::min(W, x + ww); ++px) {
            uint8_t* d = &c[(size_t(py) * size_t(W) + size_t(px)) * 4];
            const bool edge = px == x || py == y || px == x + ww - 1 || py == y + hh - 1;
            for (int k = 0; k < 3; ++k)
                d[k] = edge ? uint8_t(0x90) : uint8_t(12);
            d[3] = 255;
        }
}

// a sight at canvas pixel (cx, cy) as the game draws it at 480 lines (the Classic page is 640 x 480, scaled as a
// whole); fit: at most that many pixels
void Frontend::cSightArt(const SightArt& a, int cx, int cy, int fit)
{
    if (a.w <= 0 || a.h <= 0 || a.rgba.size() < size_t(a.w) * size_t(a.h) * 4 || !canvas_)
        return;
    std::vector<uint8_t>& c = *canvas_;
    const int W = cw_ * sc_, H = int(c.size() / 4) / std::max(1, W);
    float k = 1.0f;
    if (fit > 0 && std::max(a.dw, a.dh) > fit)
        k = float(fit) / float(std::max(a.dw, a.dh));
    const int DW = std::max(1, int(std::lround(float(a.dw) * k))), DH = std::max(1, int(std::lround(float(a.dh) * k)));
    const int X0 = cx - int(std::lround(float(a.cx) * k)), Y0 = cy - int(std::lround(float(a.cy) * k));
    for (int j = 0; j < DH; ++j)
        for (int i = 0; i < DW; ++i) {
            const int px = X0 + i, py = Y0 + j;
            if (px < 0 || py < 0 || px >= W || py >= H)
                continue;
            // (nearest, as the 640 x 480 page is drawn; a scaled picture: its pixel's middle)
            const int sx = std::clamp(int((float(i) + 0.5f) * float(a.w) / float(DW)), 0, a.w - 1);
            const int sy = std::clamp(int((float(j) + 0.5f) * float(a.h) / float(DH)), 0, a.h - 1);
            const uint8_t* s = &a.rgba[(size_t(sy) * size_t(a.w) + size_t(sx)) * 4];
            if (!s[3])
                continue;
            uint8_t* d = &c[(size_t(py) * size_t(W) + size_t(px)) * 4];
            for (int q = 0; q < 3; ++q)
                d[q] = uint8_t((s[q] * s[3] + d[q] * (255 - s[3]) + 127) / 255);
            d[3] = 255;
        }
}

void Frontend::drawCItemIcon(Widget& w)
{
    int x, y, ww, hh;
    absRect(w, x, y, ww, hh);
    if (const SightArt* a = rowArt(w.user, w.id, artSeat, kHeight); a && a->w > 0)
        cSightArt(*a, x + ww / 2, y + hh / 2, std::min(ww, hh) - 2);
}

bool Frontend::load(const std::string& pcInstall, std::string& err)
{
    if (!spr_.load(pcInstall, err, 1, 0, "MENU"))
        return false;
    // the splash (0x419FDC: SEGA logo 0xA1, title art 0xAA) is coloured through TITLE.TBL / TITLE.PAL: its backdrop is
    // then THOTD's (222, 222, 231), through MENU it was a neutral (223, 223, 223); the menu pictures keep MENU
    std::string terr;
    if (!splashSpr_.load(pcInstall, terr, 1, 0, "TITLE"))
        splashSpr_ = spr_;
    if (!spr_.width(0xD3) || !spr_.width(0xF9) || !spr_.width(glyph('A', 0x20000))) {
        err = "SPRITE/SPR.RBH: the menu pictures are missing";
        return false;
    }
    return true;
}

void Frontend::start()
{
    px_ = py_ = 5;
    attractDue_ = false;
    clickState_ = 0;
    clickTimer_ = -1;
    cursorFrame_ = 0;
    mainGroup_ = 0;   // (menu2: the top of the main menu)
    if (onCue) {
        onCue(1);
        onCue(menuCue);   // default 22, the Chapter 4 song the PC sends; the "Menu music" setting (frontend.h)
    }
    screenMainMenu();
}

// ---- frontend_input 0x41DA18: Escape runs the handler of the widget tagged 1 (else 3) ----
void Frontend::escapeKey()
{
    std::function<void(Widget*)> find;
    Widget* hit = nullptr;
    find = [&](Widget* w) {
        for (; w && !hit; w = w->next) {
            if (w->tag == 1 && w->activate) { hit = w; return; }
            find(w->child);
        }
    };
    find(root_);
    if (hit) {
        Widget& w = *hit;
        const auto fn = w.activate;
        fn(w);
    }
}

// ---- 0x420A60: the pointer (arrows with acceleration, the mouse), the press -> click animation -> activation ----
void Frontend::pointerFrame(const FrontendInput& in, uint32_t nowMs)
{
    const int px0 = px_, py0 = py_;   // 0xD5A970 / 0xD5A974: last frame's pointer
    if (idleReset_) {                 // a screen builder's timeGetTime into 0xD5A960 (0x421119 in screen 1)
        idleSince_ = nowMs;
        idleReset_ = false;
    }
    if (clickState_ == 0) {
        const bool held[4] = {in.up, in.down, in.left, in.right};
        for (int k = 0; k < 4; ++k) {
            if (!held[k]) {
                keyHeld_[k] = false;
                continue;
            }
            keyStep_[k] = keyHeld_[k] ? std::min((keyStep_[k] >> 8) * 0x180, 0xB0000) : 0x10000;
            const int d = keyStep_[k] >> 16;
            if (k == 0) py_ = std::max(py_ - d, 0);
            if (k == 1) py_ = std::min(py_ + d, kHeight - 1);
            if (k == 2) px_ = std::max(px_ - d, 0);
            if (k == 3) px_ = std::min(px_ + d, cw_ - 1);
            keyHeld_[k] = true;
        }
    }
    activateMode_ = 0;
    if (clickSound_) {
        if (onCue)
            onCue(28);   // the click: GAMESNDS sample 13
        clickSound_ = false;
    }
    if (in.press) {
        if (clickState_ == 4)
            activateMode_ = 1;   // a second press during the animation activates at once
        clickSound_ = true;
        clickState_ = 3;
    }
    // (held without an edge: mode 2, the widgets flagged 0x8000; none on the ported screens)
    if (clickState_ == 5) {
        clickState_ = 0;
        activateMode_ = 1;
    }
    if (activateMode_ != 0) {
        idleSince_ = nowMs;   // 0x420DCA: an activation is activity
        activate();
    }
    if (clickState_ == 0 && in.mouseMoved) {
        px_ = std::clamp(in.mouseX, 0, cw_ - 3);
        py_ = std::clamp(in.mouseY, 0, kHeight - 3);
    }
    // 0x420ED0..0x420F2B: the pointer moved -> activity; idle for 30 s (10 s with the debug flag 0x5CD374:
    // ((n * 31 * 4 + n) * 8 = n * 1000 ms) on screen 1 (0x5CE8A8 == 1) -> frontend_state 6, the attract.
    // UNVERIFIED (teaser): read from the disassembly; the harness capture (menuart) went back to the attract
    // after about 30 s idle
    if (px_ != px0 || py_ != py0)
        idleSince_ = nowMs;
    const uint32_t idleMs = uint32_t(debugItems_ ? 10 : 30) * 1000u;
    // (liveattract, user 2026-10-21: not in the Remake style: its background is the attract itself)
    if (nowMs - idleSince_ >= idleMs && screen_ == 1 && style != kStyleRemake)
        attractDue_ = true;
}

// ---- 0x42095C: the click's frames 0xF9..0xFC, one every 25 ms, then state 5 (activate next frame) ----
void Frontend::clickFrames(uint32_t nowMs)
{
    if (clickState_ == 5) {
        clickTimer_ = -1;
        cursorFrame_ = 0;
    }
    if (clickState_ == 3) {
        cursorFrame_ = 0;
        clickState_ = 4;
        clickTimer_ = 0;
        clickTime_ = nowMs;
    }
    if (clickTimer_ != -1) {
        if (int(nowMs - clickTime_) > 0x19) {
            ++cursorFrame_;
            clickTime_ = nowMs;
        }
        if (cursorFrame_ > 3) {
            clickTimer_ = -1;
            clickState_ = 5;
            cursorFrame_ = 0;
        }
    }
}

// ---- 0x419FDC: the start-up window. Message 0 (create): the time into 0xD5A688; stage 0, or 1 when 0x5CD350 is
// set (then cleared). Message 0xD (0x41A140, each tick): input = 0x419F54 (a press edge of a key in the list
// 0x5CE6BC: Esc, 1-0, Tab, Shift, Ctrl, Alt, the keypad, the letters ...) or 0x419FC0 (mouse button 0 / 1).
// Stage 0: sprite 0xA1; input or more than 3000 ms (0xBB8) -> stage 1, the time again. Stage 1: sprite 0xAA; input
// -> 0x565350(window, 9, 1) (the window's end, "skipped"); more than 5000 ms (0x1388) -> 0x565350(window, 9, 0).
// Paint 0x419F18: the sprite at (0, 0), no fade. UNVERIFIED (teaser): read from the disassembly; the harness capture
// (boot_idle) shows the SEGA logo, then the title art, then the attract ----
void Frontend::splashStart(uint32_t nowMs, bool skipLogo, bool logoOnly)
{
    splashLogoOnly_ = logoOnly;
    splash_ = skipLogo ? 1 : 0;
    splashTime_ = nowMs;
    splashSprite_ = skipLogo ? 0xAA : 0xA1;
}

// feat29 (user 2026-10-29: "Add the SEGA screen to the Remake-style menu before the game loads up"): the Remake
// style's splash was a black screen (only the Classic one drew the PC's picture). The picture (the PC's 640 x 480
// SEGA logo 0xA1 / title art 0xAA through TITLE.PAL) scaled to the window's height (bilinear), centred
void Frontend::splashRemake(std::vector<uint8_t>& canvas)
{
    std::vector<uint8_t>& img = splashImage_[splashSprite_];
    if (img.empty()) {
        img.assign(size_t(kWidth) * kHeight * 4, 0);
        for (size_t i = 3; i < img.size(); i += 4)
            img[i] = 255;
        splashSpr_.draw(img, splashSprite_, 0, 0);
    }
    const int W = rpw(), H = rph();
    if (W <= 0 || H <= 0 || canvas.size() < size_t(W) * size_t(H) * 4)
        return;
    const float s = float(H) / float(kHeight);
    const float x0 = (float(W) - float(kWidth) * s) / 2;
    // the sides: the logo's backdrop (a pixel at its left edge) so it fills the wide screen; the title art: black
    static const uint8_t kBlack[4] = {0, 0, 0, 255};
    const uint8_t* side = splashSprite_ == 0xA1 ? &img[(size_t(kHeight / 2) * kWidth + 8) * 4] : kBlack;
    for (int y = 0; y < H; ++y) {
        const float sy = std::clamp((float(y) + 0.5f) / s - 0.5f, 0.0f, float(kHeight - 1));
        const int iy = std::min(int(sy), kHeight - 2);
        const float fy = sy - float(iy);
        for (int x = 0; x < W; ++x) {
            uint8_t* d = &canvas[(size_t(y) * size_t(W) + size_t(x)) * 4];
            const float sx = (float(x) + 0.5f - x0) / s - 0.5f;
            if (sx < -0.5f || sx > float(kWidth) - 0.5f) {
                d[0] = side[0], d[1] = side[1], d[2] = side[2], d[3] = 255;
                continue;
            }
            const float cx = std::clamp(sx, 0.0f, float(kWidth - 1));
            const int ix = std::min(int(cx), kWidth - 2);
            const float fx = cx - float(ix);
            const uint8_t* p = &img[(size_t(iy) * kWidth + size_t(ix)) * 4];
            for (int ch = 0; ch < 3; ++ch) {
                const float a = float(p[ch]) + (float(p[4 + ch]) - float(p[ch])) * fx;
                const float b = float(p[kWidth * 4 + ch]) + (float(p[kWidth * 4 + 4 + ch]) - float(p[kWidth * 4 + ch])) * fx;
                d[ch] = uint8_t(std::lround(a + (b - a) * fy));
            }
            d[3] = 255;
        }
    }
}

void Frontend::splashTick(const FrontendInput& in, uint32_t nowMs)
{
    const bool input = in.anyKey;
    if (splash_ == 0) {
        splashSprite_ = 0xA1;
        if (input || int(nowMs - splashTime_) > 0xBB8) {
            splashTime_ = nowMs;
            splash_ = splashLogoOnly_ ? -1 : 1;   // (liveattract: the Remake style's boot: the logo, then the attract)
        }
    } else if (splash_ == 1) {
        splashSprite_ = 0xAA;
        if (input || int(nowMs - splashTime_) > 0x1388)
            splash_ = -1;   // 0x565350(window, 9, ...): the start-up window ends
    }
}

void Frontend::update(const FrontendInput& inRaw, uint32_t nowMs)
{
    ++frameCount_;
    if (splash_ >= 0) {
        splashTick(inRaw, nowMs);
        return;
    }
    // blood2 (user 2026-10-21, as the Remake): the Remake main menu idle 5 s fades away (the footage alone); any input
    // brings it back at once and is not used for anything else
    const FrontendInput none;
    const bool anyIn = inRaw.press || inRaw.escape || inRaw.up || inRaw.down || inRaw.left || inRaw.right || inRaw.anyKey ||
                       inRaw.tabPrev || inRaw.tabNext || inRaw.newSplat || !inRaw.typed.empty() || !inRaw.nameKeys.empty() || inRaw.wheel || inRaw.alt ||
                       (inRaw.mouseMoved && (inRaw.mouseX != px_ || inRaw.mouseY != py_));
    // remakefix (user 2026-10-22 "the menu does not fade back smoothly like the Remake game"): both ways an eased fade
    // (out 0.6 s after 5 s idle, back in 0.45 s on any input), every layer together (Hud::drawRemake: the backdrop
    // lighter, the rest at 1 - fade); the input that wakes it (a key, a click; the menu more than half gone) does
    // nothing else, a mouse move also moves the hand
    const bool mainShown = sc_ > 1 && screen_ == 1;
    const bool reset = fadeReset_ || !mainShown;
    if (reset || anyIn)
        lastInputMs_ = nowMs, fadeReset_ = false;
    const uint32_t dt = std::min<uint32_t>(nowMs - fadeLastMs_, 100);
    fadeLastMs_ = nowMs;
    const bool waking = anyIn && fadeLin_ > 0.5f;
    if (reset)
        fadeLin_ = 0.0f;
    else if (nowMs - lastInputMs_ >= 5000)
        fadeLin_ = std::min(1.0f, fadeLin_ + float(dt) / 600.0f);
    else
        fadeLin_ = std::max(0.0f, fadeLin_ - float(dt) / 450.0f);
    menuFade_ = fadeLin_ * fadeLin_ * (3.0f - 2.0f * fadeLin_);
    FrontendInput wake;
    if (waking) {   // (only the pointer's move; the held arrows count as held already: no step when they come back)
        wake.mouseMoved = inRaw.mouseMoved, wake.mouseX = inRaw.mouseX, wake.mouseY = inRaw.mouseY;
        wake.pad = inRaw.pad, wake.keyboard = inRaw.keyboard;
        const bool held[4] = {inRaw.up, inRaw.down, inRaw.left, inRaw.right};
        for (int k = 0; k < 4; ++k)
            navHeld_[k] = held[k], navNext_[k] = nowMs + 380;
    }
    (void)none;
    FrontendInput in = waking ? wake : inRaw;   // (menufb: a copy: the scroll bars and the range use the press up)
    blink_ = (frameCount_ & 8) != 0;
    nowMs_ = nowMs;
    // padglyphs: Button Prompts: one family always (its usual pad), the keyboard always, or Auto (the device used last)
    const int forced = padForced_ ? 0 : promptChoiceStyle(promptChoice);
    if (forced < 0) {
        padPrompts_ = false;
    } else if (forced > 0) {
        padPrompts_ = true, padType_ = forced;
    } else if (in.pad) {
        padPrompts_ = true;
        if (in.padType) padType_ = in.padType;   // (padpass: the prompts name that pad's buttons)
    } else if ((in.keyboard || in.mouseMoved) && !padForced_) padPrompts_ = false;
    if (in.mouseMoved) handShown_ = true;   // remakemenu: the bony hand while the mouse is used
    if (scrollBarInput(in)) {   // menufb: a scroll bar clicked / dragged: nothing else this frame
        if (in.escape) sbDrag_ = -1;
        return;
    }
    rangeTick(in, nowMs);   // menufb: the Firing Range's aims and shots (a shot's click used up)
    if (in.escape && ddOpen()) {   // remakefix: Escape / B closes the dropdown first
        remakeCue();
        ddClose();
        return;
    }
    if (in.escape && cdOpen()) {   // optui: the Classic style's dropdown too
        cdClose();
        return;
    }
    if (in.escape && castCd_ >= 0 && screen_ == 19 && sc_ == 1) {   // galact: the Classic Actors list
        castCd_ = -1;
        screenScenes();
        return;
    }
    if (sc_ == 1 && screen_ == 100) {   // optui: the Classic settings pages: a right-click opens a row's list, the wheel
        if (in.mouseMoved)              // scrolls it (or turns the pages)
            px_ = std::clamp(in.mouseX, 0, cw_ - 3), py_ = std::clamp(in.mouseY, 0, kHeight - 3);
        if (in.alt && !cdOpen())
            for (const RowRect& r : cRows_) {
                const Extra& e = extras[size_t(r.i)];
                const bool on = e.click && (!e.off || e.off().empty());
                if (on && e.choices.size() >= 2 && e.value && px_ >= r.x && px_ < r.x + r.w && py_ >= r.y && py_ < r.y + r.h) {
                    cdRow_ = r.i, cdKey_ = extrasTitle, cdScroll_ = -1;
                    if (onCue) onCue(28);
                    screenExtras();
                    return;
                }
            }
        else if (in.alt && cdOpen()) {
            cdClose();
            return;
        }
        if (in.wheel) {
            if (cdOpen())
                cdScroll_ -= in.wheel;
            else if (int(extras.size()) > kExtrasPerPage) {
                const int pages = (int(extras.size()) + kExtrasPerPage - 1) / kExtrasPerPage;
                extrasPage = std::clamp(extrasPage - in.wheel, 0, pages - 1);
            }
            screenExtras();
        }
    }
    if (in.wheel && screen_ == 19 && galleryRE() && reSceneTop_ > 0 && castCd_ < 0) {   // the Cut-scenes list's wheel (2 lines a notch)
        reSceneFirst_ = std::clamp(reSceneFirst_ - in.wheel * 2, 0, reSceneTop_);
        screenScenes();
        return;
    }
    if (in.escape) {
        const int screen = screen_;
        if (sc_ > 1)
            remakeCue();   // (remakefix: the Remake style's back sound)
        escapeKey();
        if (screen_ != screen || !root_)
            return;
    }
    secretCodes(in);
    if (screen_ == 22 && ((in.digit >= 1 && in.digit <= 4) || in.tabPrev || in.tabNext)) {   // playerpick: 1-4, Q / E, LB / RB
        if (sc_ > 1)
            remakeCue();
        pickPlayers(in.digit ? in.digit : pickN_ + (in.tabNext ? 1 : -1), true);   // (the page has no tabs: the rest as usual)
    }
    if (screen_ == 15)   // the name entry's keys (0x425188)
        for (const int k : in.nameKeys) {
            nameKey(k);
            if (screen_ != 15)
                return;
        }
    if (sc_ > 1) {   // remakemenu: a Remake screen's keys (the focus, not the hand); the mouse as before
        FrontendInput r = in;
        remakeNav(r, nowMs);
        r.up = r.down = r.left = r.right = false;
        pointerFrame(r, nowMs);
    } else
        pointerFrame(in, nowMs);
    // 0x427938: the hover walk (end of 0x420A60)
    if (sc_ > 1 && root_) {   // remakemenu: the last frame's marks off (a frame not drawn would keep them: the focus moved)
        std::function<void(Widget*)> clear = [&](Widget* w) {
            for (; w; w = w->next) {
                w->flags &= ~0x40000u;
                clear(w->child);
            }
        };
        clear(root_);
    }
    walking_ = true;
    stackReset();
    help_.clear();
    if (root_)
        hoverWalk(root_);
    walking_ = false;
    if (snapAim && root_) {   // Light Gun Edition: nothing under the gun: the nearest button / row is lit (what a shot takes)
        bool lit = false;
        std::function<void(Widget*)> any = [&](Widget* w) {
            for (; w && !lit; w = w->next) {
                lit = lit || ((w->flags & 0x40000) && w->activate);
                any(w->child);
            }
        };
        any(root_);
        int sx, sy;
        if (!lit && snapPoint(sx, sy)) {
            const int px = px_, py = py_;
            px_ = sx, py_ = sy;
            walking_ = true;
            stackReset();
            help_.clear();
            hoverWalk(root_);
            walking_ = false;
            px_ = px, py_ = py;
        }
    }
    clickFrames(nowMs);
}

void Frontend::render(std::vector<uint8_t>& canvas)
{
    if (sc_ > 1 && splash_ < 0) {   // remakemenu: a Remake screen (frontend_remake.cpp: its layers, kept between frames)
        canvas_ = &canvas;
        remakeRender();
        canvas_ = nullptr;
        return;
    }
    canvas.assign(size_t(canvasPixelsW()) * size_t(canvasPixelsH()) * 4, 0);
    const uint8_t opaque = classicBackdrop() && splash_ < 0 ? 0 : 255;   // remakepages: the Gallery over the backdrop
    for (size_t i = 3; i < canvas.size(); i += 4)
        canvas[i] = opaque;
    canvas_ = &canvas;
    if (splash_ >= 0) {   // 0x419F18
        if (sc_ == 1)
            splashSpr_.draw(canvas, splashSprite_, (cw_ - kWidth) / 2, 0, 0, 0, cw_, kHeight, cw_);
        else
            splashRemake(canvas);   // (feat29: was black)
        canvas_ = nullptr;
        return;
    }
    if (sc_ > 1) {   // remakemenu: a Remake screen (frontend_remake.cpp)
        remakeRender();
        canvas_ = nullptr;
        return;
    }
    walking_ = true;
    stackReset();
    if (root_)
        drawWalk(root_);
    walking_ = false;
    if (galleryFull()) {   // gallery3: the Gallery's full view: its own lines only (no help line, no hand)
        canvas_ = nullptr;
        return;
    }
    drawHelp();
    if (const std::string h = hintFor(); !h.empty())   // uxpolish: the controls line at the very bottom
        drawSmall(h, 0x1D5, true);
    if (!hideHand && !rangeHand_)   // (menufb: in the Firing Range the crosshair, no hand)
        drawCursor();
    canvas_ = nullptr;
}

void Frontend::frame(std::vector<uint8_t>& canvas)
{
    walking_ = true;
    stackReset();
    help_.clear();
    if (root_)
        hoverWalk(root_);
    walking_ = false;
    render(canvas);
}

// lang (langTour): each shot's front end handed on after it is drawn; its overflows listed
static std::function<void(Frontend&)> gShotAfter;
static bool gShotCheck = false;

static void setEnv(const char* k, const std::string& v)
{
#ifdef _WIN32
    _putenv_s(k, v.c_str());
#else
    if (v.empty()) unsetenv(k);
    else setenv(k, v.c_str(), 1);
#endif
}

int langTour(const std::string& pcInstall, const std::string& pngDir, const std::vector<LangPage>& extra, bool check,
             const std::function<void(Frontend&, const std::string&)>& each)
{
    struct Shot { int screen, page; const char* name; };
    std::vector<Shot> shots = {
        {1, 0, "main"}, {1, 1, "main_more_modes"}, {1, 2, "main_extras"}, {21, 0, "quit"}, {2, 0, "options"},
        {117, 0, "advanced"}, {118, 0, "mod_settings"}, {16, 0, "coop"}, {18, 0, "keys"}, {18, 1, "keys_pad"}, {4, 0, "controller"},
        {4, 1, "controller_2"}, {109, 0, "pcmode_scores"}, {9, 0, "chapters"}, {7, 0, "bossmode"}, {19, 0, "cutscenes"},
        {20, 0, "jukebox"}, {17, 0, "gallery"}, {116, 0, "pcmode"}, {15, 0, "name_entry"}, {119, 0, "firing_range"}, {121, 0, "language"}};
    static const char* const kCat[kMenuCategories] = {"video", "audio", "controls", "gameplay", "mods", "system"};
    for (int k = 0; k < kMenuCategories; ++k)
        for (int p = 0; p < 3; ++p)
            shots.push_back({k == kCatSystem ? 115 : 10 + k, p, kCat[k]});   // (15 is the name entry)
    for (int t = 0; t < 8; ++t)
        shots.push_back({8, t, "high_scores"});
    for (int b = 32; b <= 36; ++b)
        shots.push_back({b, 0, "boss_times"});
    int n = 0;
    gShotCheck = check;
    // sweep (testing): HOTD_LANG_SIZE=WxH draws the tour at another size (default 1280 x 720)
    int tw = 1280, th = 720;
    if (const char* sz = std::getenv("HOTD_LANG_SIZE"); sz && std::sscanf(sz, "%dx%d", &tw, &th) != 2)
        tw = 1280, th = 720;
    for (int style = 0; style < 2; ++style) {
        setEnv("HOTD_FE_STYLE", style ? "remake" : "classic");
        auto one = [&](int screen, int page, const std::string& name, const std::function<bool(Frontend&)>& setup) {
            setEnv("HOTD_FE_PAGE", std::to_string(page));
            const std::string label = std::string(style ? "remake_" : "classic_") + name + (page ? "_" + std::to_string(page) : "");
            gShotAfter = [&](Frontend& fe) { each(fe, label); };
            const std::string png = pngDir.empty() ? std::string() : pngDir + "/" + label + ".png";
            if (frontendShot(pcInstall, png, 600 * tw / 1280, 300 * th / 720, tw, th, screen, setup) == 0) ++n;
        };
        for (const Shot& s : shots)
            one(s.screen, s.page, s.name, nullptr);
        for (const LangPage& p : extra)
            one(p.screen, 0, p.name, p.setup);
    }
    setEnv("HOTD_FE_PAGE", "");
    setEnv("HOTD_FE_STYLE", "");
    gShotAfter = nullptr;
    gShotCheck = false;
    return n;
}

static int gShotJbSel = 1;   // leftovers: the sample Jukebox's chosen row (frontendShot)

int frontendShot(const std::string& pcInstall, const std::string& png, int cx, int cy, int width, int height, int screen,
                 const std::function<bool(Frontend&)>& setup)
{
    if (const char* js = std::getenv("HOTD_FE_JBSEL"))
        gShotJbSel = std::atoi(js);
    Frontend fe;
    fe.langCheck = gShotCheck;
    std::string err;
    if (!fe.load(pcInstall, err)) {
        std::fprintf(stderr, "front end: %s\n", err.c_str());
        return 1;
    }
    if (const char* pd = std::getenv("HOTD_FE_PMPORTRAITS"))   // pmfix2: the PC Mode extras' portraits from that folder
        pmPortraitsLoad(fe, pd);
    // the host's set-up (main.cpp's enterMenu) with the default settings: the categories, Sonic Mode, Co-op, chapters
    static LaunchConfig cfg;   // (rows keep a reference)
    // optui: the crosshair preview: the PC's sight sprites from the PC folder; HOTD_FE_STILL=<png>: its game picture (the
    // live attract's saved still; none: the hall), HOTD_FE_CROSSHAIR=<style>[,size,thickness] (sights.h numbers),
    // HOTD_FE_MODELS=pc: the PC models (Auto's sight)
    MenuHooks hooks;
    hooks.sightSprites = [pcInstall]() -> const assets::PcSprites* {
        static assets::PcSprites spr;
        static bool tried = false;
        std::string e2;
        if (!tried)
            tried = true, spr.load(pcInstall, e2, 1, 0);
        return spr.loaded() ? &spr : nullptr;
    };
    if (const char* st = std::getenv("HOTD_FE_STILL"))
        fe.previewStills.push_back(st);
    hooks.promptStyleNow = [&fe]() { return fe.padPromptsShown() ? fe.padType() : 0; };   // (relmenu: HOTD_FE_PAD's family)
    if (const char* xh = std::getenv("HOTD_FE_CROSSHAIR"))
        for (SeatSight& s : cfg.sights)   // (firing range: every player's; HOTD_FE_CROSSHAIR=style,size,thickness[,colour])
            std::sscanf(xh, "%d,%d,%d,%d", &s.style, &s.size, &s.thick, &s.colour);
    if (const char* md = std::getenv("HOTD_FE_MODELS"))
        cfg.models = std::string(md) == "pc" ? 1 : 0;
    // fix28 / menus3: HOTD_FE_PREVIEWS=<folder>: the settings' pictures from the attract frames rendered there
    // (base_ / bridge_<arcade|pc>.png: a game's settings folder's previews6\, or HOTD_PREVIEW_TEST's)
    if (const char* pv = std::getenv("HOTD_FE_PREVIEWS")) {
        const std::string dir = pv;
        hooks.previewPic = [dir](const std::string& name) {
            if (name.empty())
                return std::string("x");
            const std::string key = previewKey(name, cfg, true);
            const std::string f = key.empty() ? std::string()
                                              : previewBaseFile(dir, std::atoi(key.c_str() + key.find('|') + 1),
                                                                name.compare(0, 6, "models") == 0 ? 1 : 0);
            FILE* t = f.empty() ? nullptr : std::fopen(f.c_str(), "rb");
            if (t)
                std::fclose(t);
            return t ? "mem:" + key : std::string();
        };
        fe.previewImage = [dir, h = hooks](const std::string& key, std::vector<uint8_t>& rgba, int& w, int& ht) {
            return previewRender(key, dir, cfg, h, rgba, w, ht);
        };
    }
    // mods (docs/modding.md): the Mods page with the mods in data\mods (and the built-in Ragdoll)
    static const std::vector<ModPack> mods = [] {
        std::vector<std::string> dirs;
        for (const std::string& d : lang::dataDirs()) dirs.push_back(d + "/data/mods");
        return findMods(dirs);
    }();
    hooks.modsRows = []() { return menuModRows(cfg, mods, nullptr, nullptr); };
    for (int k = 0; k < kMenuCategories; ++k)
        fe.tabs.push_back(menuCategoryName(k));
    fe.sonicValue = []() { return std::string(sonicName(cfg.sonic)); };
    fe.onSonic = []() {};
    fe.onCoop = []() {};
    fe.openGallery = []() {};
    fe.openJukebox = []() {};
    fe.onChapter = [](int) {};
    fe.startBossMode = []() {};   // (as the host: Boss Mode and PC Mode are on)
    fe.startPcMode = []() {};
    for (int k = 0; k < 4; ++k)
        fe.chapterEnabled[k] = chapterPlayable(k);
    fe.chapterEnabled[4] = chapterPlayable(3);
    fe.rankingTables = menuRankingTables();
    fe.onTab = [&fe, hooks](int k) {   // (as the host's openCategory)
        fe.extras = menuCategoryRows(k, cfg, hooks);
        fe.extrasTitle = menuCategoryName(k);
        fe.extrasTab = k;
        fe.extrasExit = nullptr;
        fe.screenExtras();
    };
    // remakemenu: HOTD_FE_STYLE=remake: the Remake style (its screens as wide as width x height); HOTD_FE_FOCUS=n: the
    // keyboard's focus moved down n times (the hand hidden), else the hand at (cx, cy)
    const char* styleEnv = std::getenv("HOTD_FE_STYLE");
    if (styleEnv && std::string(styleEnv) == "remake")
        fe.style = Frontend::kStyleRemake;
    if (const char* rg = std::getenv("HOTD_FE_REGIONS"))   // remake2: the arcade regions (the blood's art; main.cpp)
        fe.setArcadeRegions(rg);
    fe.setViewAspect(float(width) / float(std::max(1, height)));
    fe.setOutputHeight(height);
    if (const char* sd = std::getenv("HOTD_FE_SPLAT"))   // blood2: the splatter's pattern (launcher.ini menublood; 0 the first)
        fe.splatSeed = uint32_t(std::strtoul(sd, nullptr, 10));
    // blood2: HOTD_FE_BLOOD=N (0 red .. 4 yellow) or RRGGBB (a custom colour, 6 hex digits): the menu's blood colour
    if (const char* bl = std::getenv("HOTD_FE_BLOOD")) {
        if (std::strlen(bl) == 6)
            fe.bloodRgb = int(std::strtoul(bl, nullptr, 16) & 0xFFFFFF), cfg.blood = 5, cfg.bloodRgb = fe.bloodRgb;   // (menufb: the rows too)
        else
            fe.options.blood = std::clamp(std::atoi(bl), 0, 4);
    }
    // HOTD_FE_MS=<ms>: the screens' clock (the hall's drift)
    const uint32_t shotMs = std::getenv("HOTD_FE_MS") ? uint32_t(std::atoi(std::getenv("HOTD_FE_MS"))) : 0u;
    const char* pgEnv = std::getenv("HOTD_FE_PAGE");   // the page / table to shoot
    const int pg = pgEnv ? std::atoi(pgEnv) : 0;
    // padpass: HOTD_FE_PAD=<style>: the prompts as that pad's (a pad used last). padglyphs: every style
    // (pad_prompts.h padStyleKey: xbox360, xboxone, elite, ps3, ps4, ps5, edge, switch, joycon, joyconl, joyconr,
    // switch2, gamecube, n64, snes, wiiupro, wii, steamcontroller, steamdeck, generic; or xbox, playstation, nintendo,
    // steam); "keyboard": the keys
    // padglyphs: HOTD_FE_BUTTONS=xelu|kenney|simple: the Button Style
    if (const char* bs = std::getenv("HOTD_FE_BUTTONS"))
        fe.buttonStyle = std::string(bs) == "kenney" ? kPackKenney : std::string(bs) == "simple" ? kPackSimple : kPackXelu;
    if (const char* fp = std::getenv("HOTD_FE_FREEPLAY"))   // relmenu: HOTD_FE_FREEPLAY=1: Free Play On (Options' Continues)
        fe.freePlay = cfg.freePlay = std::atoi(fp) != 0;
    if (const char* io = std::getenv("HOTD_FE_OUTLINE"))   // menus3: HOTD_FE_OUTLINE=1: Icon Outline On
        fe.iconOutline = std::atoi(io) != 0;
    if (const char* pd = std::getenv("HOTD_FE_PAD")) {
        const int st = padStyleByKey(pd);
        if (st >= 0) fe.showPadPrompts(st);
        else if (std::string(pd) != "keyboard") std::fprintf(stderr, "HOTD_FE_PAD: unknown pad '%s' (Xbox)\n", pd), fe.showPadPrompts(SDL_GAMEPAD_TYPE_XBOXONE);
    }
    if (screen == 120) {   // a page the caller builds (the Online Co-op pages)
        if (!setup || !setup(fe)) {
            std::fprintf(stderr, "front end: nothing to shoot for screen 120\n");
            return 1;
        }
    } else if (screen == 7) {   // Boss Mode's boss select (bossmode)
        fe.screenBossMode();
    } else if (screen == 15) {   // the name entry; HOTD_FE_NAME_KEYS: typed first ('<' Backspace, '[' Left, '#' Enter)
        fe.screenNameEntry("00:31:25");
        fe.onNameEntered = [](const std::string& n) { std::printf("NAME ENTRY: name=%s\n", n.c_str()); };
        if (const char* k = std::getenv("HOTD_FE_NAME_KEYS")) {
            FrontendInput in;
            for (const char* p = k; *p; ++p)
                in.nameKeys.push_back(*p == '<' ? kKeyBack : *p == '[' ? kKeyLeft : *p == '#' ? kKeyEnter : *p);
            fe.update(in, 0);
        }
    } else if (screen >= 32 && screen <= 36) {   // a boss times table (the PC's defaults; the third row highlighted)
        fe.bossRows = [](int m) {
            const BossTimes t;
            std::vector<Frontend::RankRow> rows;
            for (const BossTimes::Row& r : t.rows[m - 2])
                rows.push_back({r.name, BossMode::timeText(r.ms), r.ms});
            return rows;
        };
        fe.screenBossTimes(screen - 30, 2);
    } else if (screen == 122) {   // ipolish: Controls > Light Guns' Assign page (sample devices; player 3 waiting)
        cfg.gunDev[0] = "sample-a", cfg.gunName[0] = "Sinden Lightgun";
        cfg.gunDev[1] = "sample-b", cfg.gunName[1] = "GUN4IR Pro";
        hooks.gunWaiting = []() { return 2; };
        hooks.gunWaitLeft = []() { return 8; };
        hooks.gunConnected = [](const std::string& k) { return k != "sample-b"; };
        fe.extras = menuGunAssignRows(cfg, hooks);
        fe.extrasTitle = "Light Guns";
        fe.extrasExit = []() {};
        fe.extrasPage = pg;
        fe.screenExtras();
    } else if (screen == 21) {   // Quit's question (uxpolish)
        fe.screenQuit();
    } else if (screen == 22) {   // playerpick: the players' choice (HOTD_FE_PAGE: the players, default 2; player 3 a CPU)
        fe.playersAsk = []() { return true; };
        fe.playersGet = [pg](int& n, int bot[4]) {
            n = pg >= 1 && pg <= 4 ? pg : 2;
            bot[0] = bot[1] = bot[3] = 0;
            bot[2] = 2;
        };
        fe.playersSet = [](int, const int*) {};
        fe.askPlayers("Arcade", []() {}, []() {});
    } else if (screen == 119) {   // menufb: the Firing Range (HOTD_FE_PAGE: its player 0-3; HOTD_FE_RANGE=1: shots fired)
        static int seat = 0;
        seat = std::clamp(pg, 0, 3);
        fe.artSeat = seat;
        if (const char* tf = std::getenv("HOTD_FE_TEAMFLASH")) cfg.teamFlash = std::atoi(tf);   // (1 full, 2 reduced, 3 off)
        fe.rangeFlash = cfg.teamFlash;
        fe.rangeSig = []() {
            std::string s = std::to_string(seat);
            for (const SeatSight& q : cfg.sights) s += "|" + std::to_string(q.style) + "," + std::to_string(q.colour) + "," + std::to_string(q.size);
            return s;
        };
        fe.rangeArt = [hooks](int st, int wh, SightArt& out) {
            seatSightArt(cfg.sights[st & 3], cfg.models == 1, hooks.sightSprites(), {}, st & 3, wh, out);
            return out.w > 0;
        };
        if (const char* rs = std::getenv("HOTD_FE_RANGE_SIGHTS"))   // (each player's style,colour: "3,0;4,1;6,0;7,5")
            for (int p = 0; p < 4 && rs && *rs; ++p) {
                std::sscanf(rs, "%d,%d", &cfg.sights[p].style, &cfg.sights[p].colour);
                rs = std::strchr(rs, ';');
                if (rs) ++rs;
            }
        fe.rangeReset();
        fe.extras = menuRangeRows(cfg, hooks, seat);
        fe.extrasTitle = Frontend::kRangeTitle;
        fe.extrasTab = -1;
        fe.extrasExit = []() {};
        fe.screenExtras();
    } else if (screen == 121) {   // menufb: System > Language's page
        fe.extras = menuLanguageRows(cfg, hooks);
        fe.extrasTitle = "Language";
        fe.extrasTab = -1;
        fe.extrasExit = []() {};
        fe.screenExtras();
    } else if (screen == 118) {   // mods: the Ragdoll mod's Settings page
        for (const ModPack& p : mods)
            if (p.type == "ragdoll") {
                fe.extras = menuModSettingRows(cfg, p, nullptr);
                fe.extrasTitle = p.name;
            }
        fe.extrasTab = -1;
        fe.screenExtras();
    } else if (screen == 117) {   // advopt: Gameplay's Advanced page
        fe.extras = menuAdvancedRows(cfg, hooks);
        fe.extrasTitle = "Advanced";
        fe.extrasTab = -1;
        fe.screenExtras();
    } else if (screen == 3 || screen == 115|| (screen >= 10 && screen < 10 + kMenuCategories)) {   // a category's page
        const int cat = screen == 3 ? 0 : screen == 115 ? kCatSystem : screen - 10;   // (3: Video; 115: System)
        fe.extras = menuCategoryRows(cat, cfg, hooks);
        fe.extrasTitle = menuCategoryName(cat);
        fe.extrasTab = cat;
        fe.extrasPage = pg;
        fe.screenExtras();
    } else if (screen == 19) {   // the Gallery's Cut-scenes page (the host's list; HOTD_FE_PAGE: the chosen row)
        fe.scenesText = [pg]() {
            Frontend::ScenesText t;
            t.rows = sceneRowLabels();
            t.selected = pg;
            t.line = "Chapter 2, scene 12: four bodies with 3 or 4 players";
            t.status = "Scene 12 was not reached: an example status line.";
            for (int r = 0; r < SceneView::kRoles; ++r) {   // actorswap: an example cast (every actor's name a key)
                t.castRoles.push_back(SceneView::roleName(r));
                t.castActors.push_back(SceneView::actors()[size_t((r + 4) % int(SceneView::actors().size()))].name);
            }
            return t;
        };
        fe.onScenes = [](int) {};
        fe.screenScenes();
    } else if (screen == 20) {   // the Jukebox with sample texts (the real page: HOTD_JUKEBOX_SHOT, main.cpp)
        fe.jukeboxText = []() {
            Frontend::JukeboxText t;
            t.tabs = {"Music", "Sound Effects", "Voices"};
            for (int i = 2; i <= 12; ++i) t.rows.push_back({"Track " + std::to_string(i), "Track " + std::to_string(i)});
            t.selected = gShotJbSel;   // (leftovers: HOTD_FE_JBSEL / HOTD_FE_JBTURN)
            t.playing = 1;
            t.source = "PC";
            t.otherSource = true;
            t.now = "Playing: Track 3";
            t.time = "1:23 / 5:06";
            t.progress = 271;
            t.info = "Music\\Track03, Saturn disc 03, cue 9 MUSIC_STG1, loops";
            t.mode = "Loop all";
            return t;
        };
        fe.onJukebox = [](int) {};
        fe.screenJukebox();
    } else if (screen == 123 || screen == 124) {   // feat29: the start-up splash: 123 the SEGA logo, 124 the title art
        fe.splashStart(0, screen == 124);
    } else if (screen == 17) {   // the Gallery (no model: the page only; lang: setup gives an entry's texts)
        if (setup) setup(fe);
        fe.onScenes = [](int) {};
        fe.screenGallery();
    } else if (screen == 18) {   // keybind: Keys & Buttons (player 1, the defaults; HOTD_FE_PAGE 1: the gamepad's page)
        static BindCapture cap;
        static Pads pads;   // (no pad: the Classic style's buttons)
        MenuHooks bh;
        bh.padDefaults = [](int q) { return pads.defaultBinds(q); };
        fe.extras = menuBindRows(0, cfg, cap, bh);
        fe.extrasTitle = "Keys & Buttons";
        fe.extrasExit = []() {};
        fe.extrasPage = pg;
        fe.screenExtras();
    } else if (screen == 16) {   // Co-op
        fe.extras = menuCoopRows(cfg, hooks);
        fe.extrasTitle = "Co-op";
        fe.extrasExit = []() {};
        fe.extrasPage = pg;
        fe.screenExtras();
    } else if (screen == 4) {   // pads: a Controller page (the default profile, the Motion + stick style's rows)
        const PadProfile pp;
        int nf = 0;
        const PadField* f = padFields(nf);
        fe.extras.push_back({"Pad", []() { return std::string("DualSense Wireless Controller"); }, nullptr,
                             "The pad connected for this player"});
        for (int i = 0; i < nf; ++i) {
            if (f[i].styleMember && !(f[i].styleMask & 4))
                continue;
            const int v = f[i].styleMember ? pp.s[2].*f[i].styleMember : pp.*f[i].member;
            const std::string t = f[i].names ? f[i].names[v - f[i].lo] : std::to_string(v);
            fe.extras.push_back({f[i].label, [t]() { return t; }, []() {}, padFieldHelp(f[i].key)});
        }
        fe.extras.push_back({"Feel Preset", []() { return std::string("Balanced"); }, []() {},
                             "Precise, Balanced or Fast: this style's speed settings at once (Custom: changed by hand)"});
        fe.extras.push_back({"Aim Test", []() { return std::string("Open >"); }, []() {},
                             "What the pad reads now (gyro, stick, crosshair speed) and the gyro's calibration"});
        lightGunRow(fe.extras, "Light Gun Calibration", cfg.gunCal[0], nullptr);
        fe.extrasTitle = "Controller: Player 1";
        fe.extrasExit = []() {};
        fe.extrasPage = pg;
        fe.screenExtras();
    } else if (screen == 8) {   // High Scores with sample rows
        fe.rankingTableRows = [](int t) {
            std::vector<Frontend::RankRow> r;
            for (int i = 0; i < 10; ++i) r.push_back({std::string(1, char('A' + i + t)) + "AA", "-", 50000 - i * 3000 - t * 100});
            return r;
        };
        fe.rankingTable = pg;
        fe.screenRanking();
    } else if (screen == 109) {   // (THOTD.EXE screen 9) PC Mode's high scores: the PC's defaults (pcmode_scores.h)
        fe.rankingTables.push_back("PC Mode");
        fe.rankingTableRows = [](int) {
            static const char* const kStage[4] = {"Stage 1", "Stage 2", "Stage 3", "Stage 4"};
            std::vector<Frontend::RankRow> r;
            PcModeScores t;
            if (const char* f = std::getenv("HOTD_FE_PMSCORES"))   // (pmextra testing: a pcmode_scores.txt's rows)
                t.load(f);
            for (const PcModeScores::Row& row : t.rows)
                r.push_back({row.name, kStage[row.stage & 3], row.score, row.ch});
            return r;
        };
        fe.rankingTable = int(fe.rankingTables.size()) - 1;
        fe.screenRanking();
    } else if (screen == 9)
        fe.screenChapters();
    else if (screen == 2) {
        fe.extras = menuCategoryRows(0, cfg, hooks);
        fe.screenOptions();
    } else if (screen == 116) {   // (THOTD.EXE screen 16) PC Mode's character select (HOTD_FE_PCCHARS=a,b: the two panels' characters)
        // HOTD_FE_PCSEATS=players[,ready mask[,cpu mask]] (ours, 4P): the screen for that many players, the seats in
        // the masks (bit 0 = P1) ready / computer; HOTD_FE_PCCHARS=a,b[,c,d] the panels' characters
        if (const char* pc = std::getenv("HOTD_FE_PCCHARS"))
            std::sscanf(pc, "%d,%d,%d,%d", &fe.pcChars[0], &fe.pcChars[1], &fe.pcChars[2], &fe.pcChars[3]);
        int players = 2, ready = 0, cpu = 0;
        if (const char* ps = std::getenv("HOTD_FE_PCSEATS"))
            std::sscanf(ps, "%d,%d,%d", &players, &ready, &cpu);
        const bool cpus[4] = {false, (cpu & 2) != 0, (cpu & 4) != 0, (cpu & 8) != 0};
        fe.pcModeOpen(players, cpus, 7);
        for (int p = 0; p < 4; ++p)
            if (ready & (1 << p)) fe.pcReady[p] = fe.pcChars[p] != 0;
        fe.screenPcMode();
    } else {
        if (pg >= 1 && pg <= 2) fe.mainMenuGroup(pg);   // menu2: HOTD_FE_PAGE 1 More Modes, 2 Extras
        fe.screenMainMenu();
    }
    std::vector<uint8_t> c;
    if (const char* focus = std::getenv("HOTD_FE_FOCUS"); focus && fe.remakeScreen()) {
        FrontendInput in;
        in.keyboard = true;
        fe.update(in, shotMs);   // (the first focus)
        for (int k = std::atoi(focus); k > 0; --k) {
            in.down = true;
            fe.update(in, shotMs + 2u * unsigned(k) + 1);
            in.down = false;
            fe.update(in, shotMs + 2u * unsigned(k) + 2);
        }
    } else if (fe.remakeScreen()) {   // (the mouse: the hand shown)
        FrontendInput in;
        in.mouseMoved = true;
        in.mouseX = cx, in.mouseY = cy;
        fe.update(in, shotMs);
    } else
        fe.setPointer(cx, cy);
    // leftovers: the Jukebox's list page turned as the menu does it (HOTD_FE_JBTURN=<row>: the page drawn at
    // HOTD_FE_JBSEL's row, then that row chosen and the page built again; the picture should equal HOTD_FE_JBSEL=<row>'s
    // own); with HOTD_FE_BENCH_ACT=n the turn timed n times (the GPU layers' CPU part)
    if (const char* jt = std::getenv("HOTD_FE_JBTURN"); jt && screen == 20) {
        const int from = gShotJbSel, to = std::atoi(jt);
        if (const char* ba = std::getenv("HOTD_FE_BENCH_ACT")) {
            const int n = std::max(2, std::atoi(ba));
            fe.gpuLayers = true;
            fe.render(c);
            double total = 0, most = 0;
            for (int i = 0; i < n; ++i) {
                const auto t0 = std::chrono::steady_clock::now();
                gShotJbSel = (i & 1) ? from : to;
                fe.refreshJukebox();
                fe.render(c);
                const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                total += ms, most = std::max(most, ms);
            }
            std::printf("front end: Jukebox page turned %.2f ms a frame (most %.2f; %d x %d pixels)\n", total / n, most,
                        fe.canvasPixelsW(), fe.canvasPixelsH());
            fe.gpuLayers = false;
        }
        gShotJbSel = from;
        fe.refreshJukebox();
        fe.render(c);
        gShotJbSel = to;
        fe.refreshJukebox();
    }
    // optui: HOTD_FE_BENCH_ACT=n: a settings page's actions timed (a frame each: the rows scrolled by the wheel, a row's
    // dropdown opened / closed; the GPU layers' CPU part as the menu), then HOTD_FE_DD=<row>: that row's list open
    if (const char* ba = std::getenv("HOTD_FE_BENCH_ACT")) {
        const int n = std::max(2, std::atoi(ba));
        const int ddRow = std::getenv("HOTD_FE_DD") ? std::atoi(std::getenv("HOTD_FE_DD")) : 0;
        fe.gpuLayers = true;
        fe.render(c);
        for (int act = 0; act < 2; ++act) {
            double total = 0, most = 0, upd = 0;
            for (int i = 0; i < n; ++i) {
                FrontendInput in;
                const auto t0 = std::chrono::steady_clock::now();
                if (act == 0)
                    in.wheel = (i & 1) ? 1 : -1;
                else if (i & 1)
                    in.escape = true;
                else
                    fe.openList(ddRow);
                fe.update(in, shotMs + 20u * unsigned(i + 1));
                const auto t1 = std::chrono::steady_clock::now();
                fe.render(c);
                const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                total += ms, most = std::max(most, ms);
                upd += std::chrono::duration<double, std::milli>(t1 - t0).count();
                if (std::getenv("HOTD_FE_BENCH_LOG"))
                    std::printf("  %d: list %d focus %s %.2f ms\n", i, int(fe.dropdownOpen()), fe.focusText().c_str(), ms);
            }
            std::printf("front end: %s %.2f ms a frame (most %.2f; the page built %.2f; %d x %d pixels)\n",
                        act ? "dropdown open / close" : "rows scrolled", total / n, most, upd / n, fe.canvasPixelsW(), fe.canvasPixelsH());
        }
        fe.gpuLayers = false;
    }
    if (const char* dd = std::getenv("HOTD_FE_DD"))
        fe.openList(std::atoi(dd));
    if (const char* bench = std::getenv("HOTD_FE_BENCH")) {   // remakemenu: n frames' time (still, then the focus moving)
        const int n = std::max(1, std::atoi(bench));
        // (blood2: passes 2 / 3 with the GPU's layers: the CPU's part of a frame, the uploads and draws not counted)
        for (int pass = 0; pass < 4; ++pass) {
            fe.gpuLayers = pass >= 2;
            fe.render(c);   // (a first frame not counted: the layers' first drawing for this mode)
            const auto t0 = std::chrono::steady_clock::now();
            for (int i = 0; i < n; ++i) {
                FrontendInput in;
                in.down = (pass & 1) && (i & 1) == 0;
                fe.update(in, shotMs + 20u * unsigned(i));
                fe.render(c);
            }
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            std::printf("front end: %s%s %.2f ms a frame (%d x %d pixels)\n", pass & 1 ? "focus moving" : "still",
                        pass >= 2 ? " (GPU layers, CPU part)" : "", ms / n, fe.canvasPixelsW(), fe.canvasPixelsH());
        }
        {   // blood2: a new splatter pattern's drawing (the static layer again, the splatter's tiles new)
            const auto t0 = std::chrono::steady_clock::now();
            const std::vector<uint8_t> sp = Frontend::splatterLayer(fe.canvasWidth(), float(fe.canvasPixelsH()) / float(Frontend::kHeight),
                                                                    fe.splatSeed + 1, 0xA00C08);
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            std::printf("front end: a new splatter %.1f ms (on a worker; %zu bytes)\n", ms, sp.size());
        }
        fe.gpuLayers = false;
    }
    if (screen == 119 && std::getenv("HOTD_FE_RANGE")) {   // menufb: P2 hits target 0, P3 target 1's edge, P1 (the mouse) misses
        float tx, ty, ux, uy;
        fe.rangeTargetAt(0, tx, ty);
        fe.rangeTargetAt(1, ux, uy);
        FrontendInput in;
        fe.rangeAim[1] = {true, tx, ty, true};
        fe.rangeAim[2] = {true, ux + 9, uy + 4, true};
        fe.rangeAim[3] = {true, (tx + ux) / 2, ty + 30, false};
        in.mouseMoved = true, in.mouseX = int(ux) - 40, in.mouseY = int(uy) + 20, in.press = in.click = true;
        fe.update(in, shotMs + 1000);
        for (Frontend::RangeAim& a : fe.rangeAim) a.shot = false;
        in.press = in.click = false;
        fe.update(in, shotMs + 1060);   // (the bursts half way, the flashes still there)
        std::printf("range: hits %d %d %d %d, shots %d %d %d %d\n", fe.rangeHits(0), fe.rangeHits(1), fe.rangeHits(2), fe.rangeHits(3),
                    fe.rangeShots(0), fe.rangeShots(1), fe.rangeShots(2), fe.rangeShots(3));
    }
    fe.frame(c);
    if (gShotAfter)   // lang: langTour's look at it
        gShotAfter(fe);
    if (png.empty())
        return 0;
    // fitted inside (Hud::drawPicture): pillarboxed, or letterboxed when narrower than the picture; nearest for the PC's
    // 640 x 480, bilinear (as the GL texture) for a Remake screen's 2-pixel units
    const int CW = fe.canvasPixelsW(), CH = fe.canvasPixelsH();
    const int dh = std::min(height, width * CH / CW), dw = dh * CW / CH;
    const int ox = (width - dw) / 2, oy = (height - dh) / 2;
    std::vector<uint8_t> rgb(size_t(width) * height * 3, 0);
    const bool smooth = fe.remakeScreen();
    const bool over = fe.classicBackdrop();   // remakepages: the Gallery's page over the Remake backdrop
    if (over)
        rgb = fe.backdropPicture(width, height);
    for (int y = 0; y < dh; ++y)
        for (int x = 0; x < dw; ++x) {
            if (ox + x < 0 || ox + x >= width)
                continue;
            uint8_t* d = &rgb[(size_t(y + oy) * width + size_t(ox + x)) * 3];
            if (!smooth) {
                const uint8_t* s = &c[(size_t(y * CH / dh) * CW + size_t(x * CW / dw)) * 4];
                if (over) {
                    for (int k = 0; k < 3; ++k)
                        d[k] = uint8_t((s[k] * s[3] + d[k] * (255 - s[3]) + 127) / 255);
                    continue;
                }
                d[0] = s[0], d[1] = s[1], d[2] = s[2];
                continue;
            }
            const float fx = std::clamp((x + 0.5f) * CW / dw - 0.5f, 0.0f, float(CW - 1));
            const float fy = std::clamp((y + 0.5f) * CH / dh - 0.5f, 0.0f, float(CH - 1));
            const int x0 = int(fx), y0 = int(fy), x1 = std::min(x0 + 1, CW - 1), y1 = std::min(y0 + 1, CH - 1);
            const float ax = fx - x0, ay = fy - y0;
            for (int k = 0; k < 3; ++k) {
                const float a = c[(size_t(y0) * CW + x0) * 4 + k] * (1 - ax) + c[(size_t(y0) * CW + x1) * 4 + k] * ax;
                const float b = c[(size_t(y1) * CW + x0) * 4 + k] * (1 - ax) + c[(size_t(y1) * CW + x1) * 4 + k] * ax;
                d[k] = uint8_t(std::clamp(a * (1 - ay) + b * ay + 0.5f, 0.0f, 255.0f));
            }
        }
    if (!writePng(png, width, height, rgb)) {
        std::fprintf(stderr, "front end: cannot write %s\n", png.c_str());
        return 1;
    }
    std::printf("front end: main menu -> %s\n", png.c_str());
    return 0;
}

} // namespace hotd::game
