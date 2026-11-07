// The PC port's PC Mode screen (screen 16): the character select, ported from THOTD.EXE's disassembly (0x4259AC, the
// panel 0x420080, the arrows 0x425B5C / 0x425BAC, Start 0x425990). docs/pcmode.md. The pictures and labels are the
// player's PC menu art (SPR.RBH through MENU.TBL / MENU.PAL), the characters THOTD.EXE's table 0x5CE348.
//
// UNVERIFIED (teaser): not compared with a PC harness capture of screen 16 yet.
#include "game/frontend.h"

#include "port/game.h"
#include "util/utf8.h"

#include <algorithm>
#include <string>

namespace hotd::game {

namespace {
using Chars = port::Game<port::A>;
// 0x420080's ratings: label 0x58 / 0x59 / 0x5A (Low / Medium / High) by two thresholds (0x5CE4B4.. in THOTD.EXE)
int rating(float v, float lo, float hi) { return !(lo < v) ? 0x58 : (v <= lo || hi < v) ? 0x5A : 0x59; }

// namefit: a name still too wide in the PC's small font (`shown`: nm as drawn, its translation): a short one
// ("Researcher (woman, 2)" -> "Researcher W2", "Dr. Curien (Dead)" -> "Curien (Dead)"), else the words before " ("
// cut short ("Res. (woman, 2)"); a translation the same way. nm itself when it fits
template <class Fits>
std::string shortPcName(const std::string& nm, const std::string& shown, bool on, Fits&& fits)
{
    if (fits(shown))
        return nm;
    std::vector<std::string> tries;
    if (const size_t wm = nm.find(" (woman"); wm != std::string::npos) {   // "W" / "W2" after the name
        const size_t dg = nm.find_first_of("0123456789", wm);
        const std::string head = nm.substr(0, wm);
        tries.push_back((on ? lang::tr(head) : head) + " W" + (dg == std::string::npos ? "" : nm.substr(dg, 1)));
    }
    const size_t cut = shown.rfind(" (");
    const std::string head = cut == std::string::npos ? shown : shown.substr(0, cut);
    const std::string tail = cut == std::string::npos ? std::string() : shown.substr(cut);
    if (const size_t sp = head.find(' '); sp != std::string::npos)   // (its first word dropped)
        tries.push_back(head.substr(sp + 1) + tail);
    for (const std::string& t : tries)
        if (fits(t)) return t;
    return util::utf8Fit(head, ".", [&](const std::string& h) { return fits(h + tail); }) + tail;
}
}

// pmextra: a character's name between the arrows (Classic): a name wider than the room between them (an Extra
// Character's: "Researcher (woman, 2)") in the PC's small font (a translation in the language's font: fitted);
// namefit: still too wide there: shortPcName's short name, a few pixels off the arrows
void Frontend::pcNameText(Widget* name, int ch, int room)
{
    const std::string n = Chars::pm_table(ch).name;
    int w = 0, h = 0;
    measure(n, 0x20020, w, h);
    const std::string shown = langOn() ? lang::tr(n) : n;
    const bool small = w > room && !lang::fontFor(shown);
    const std::string fit = !small ? n : shortPcName(n, shown, langOn(), [&](const std::string& t) {
        int tw = 0, th = 0;
        measureRaw(t, 0x800020, tw, th);
        return tw <= room - 12;
    });
    setText(name, fit, small ? 0x800020 : 0x20020);
    if (small)
        name->y += 7;   // (the small font's line in the middle of the 24-pixel one's)
    name->fitW = room;
}

// pmextra (docs/pcmode.md "Extra Characters"; ours): an extra character's lines under its portrait (the PC's portrait of
// the character whose rules it plays by): the group, whose rules, and when the PC files have no model of it, the body
// the PC models draw instead. Empty for the PC's own 0..6
std::vector<std::string> Frontend::pcExtraNotes(int ch)
{
    const auto* x = Chars::pm_extra(ch);
    if (!x)
        return {};
    std::vector<std::string> v = {"Extra Characters", std::string("Plays as ") + Chars::pm_table(x->base).name};
    if (!x->pcModel)
        v.push_back(std::string("PC models: ") + Chars::pm_table(x->base).name + "'s body");
    return v;
}

void Frontend::setPcPortrait(int ch, bool small, int w, int h, std::vector<uint8_t> rgba)
{
    if (!Chars::pm_extra(ch))
        return;
    int ox = 0, oy = 0;   // (where the PC's picture of its base starts: the same place)
    const auto& base = Chars::pm_table(ch);
    spr_.origin(small ? base.sprite2 : base.portrait, ox, oy);
    const int id = (small ? kPmPortraitSmall : kPmPortraitBig) + ch;
    spr_.addImage(id, w, h, std::move(rgba), ox, oy);
    glyphCache_.erase(id);   // (pmpick: the other model set's picture replaced: the Remake pages' copy too)
}

// pmpick (user 2026-10-30: an extra showed the portrait of the PC character it plays as): an extra's own picture, or
// none while it is still being made (pcPortraitsChanged then builds the page again)
int Frontend::pcPortraitId(int ch, bool small) const
{
    const auto& c = Chars::pm_table(ch);
    if (Chars::pm_extra(ch)) {
        const int id = (small ? kPmPortraitSmall : kPmPortraitBig) + ch;
        return spr_.width(id) > 0 ? id : 0;
    }
    return small ? c.sprite2 : c.portrait;
}

void Frontend::pcPortraitsChanged()
{
    if (screen_ == 16)
        screenPcMode();
    else if (screen_ == 8)
        screenRanking();
}

// (0x427E70 hitImage: frontend.cpp)

// 0x420080 (panel, player): with a character (not 0): the portrait (+0x18), Hit Points "%d" (+0x20), Chamber "%d"
// (+0x24), Bullet Damage / Reload Time / Hit Size rated Low / Medium / High; then the name (or No Player) between the
// arrows 0xD8 (hover 0xDA) and 0xD9 (hover 0xDB)
void Frontend::pcModePanel(Widget* panel, int p)
{
    const int ch = pcChars[p];
    if (ch != 0) {
        const auto& c = Chars::pm_table(ch);
        Widget* pic = widgetNew();
        addChild(panel, pic);
        setRect(pic, 0, 0, 0, 100);
        setImage(pic, pcPortraitId(ch, false), -1, 0x100020);
        const std::vector<std::string> notes = pcExtraNotes(ch);   // pmextra: the lines under the name
        for (size_t i = 0; i < notes.size(); ++i) {
            Widget* n = widgetNew();
            addChild(panel, n);
            setRect(n, 0, 0x14A + int(i) * 24, 0, 0);   // (under the name: the PC's portrait fills the room above the rows)
            setText(n, notes[i], i == 0 ? 0x20020 : 0xA0020);   // (white 24, then grey 24; the group in yellow)
            if (i == 0)
                n->draw = &Frontend::drawTextLit;
            n->fitW = 0xF8 - 40;   // (a translation kept off Start / Exit in the middle)
        }
        auto row = [&](int y, int labelId, const std::string* text, int valueLabel) {
            Widget* l = widgetNew();
            addChild(panel, l);
            setRect(l, 0, y, 0, 0);
            setLabel(l, labelId, 0x20002);
            Widget* v = widgetNew();
            addChild(panel, v);
            setRect(v, 0, y, 0, 0);
            if (text)
                setText(v, *text, 0x20004);
            else
                setLabel(v, valueLabel, 0x20004);
        };
        const std::string hp = std::to_string(c.hp), rounds = std::to_string(c.rounds);
        row(0xB8, 0x51, &hp, 0);                                                    // Hit Points:
        row(0xD0, 0x52, &rounds, 0);                                                // Chamber:
        row(0xE8, 0x53, nullptr, rating(c.damage, 0.8f, 1.2f));                    // Bullet Damage:
        row(0x100, 0x54, nullptr, rating(float(c.reload_ms), 200.0f, 300.0f));     // Reload Time:
        row(0x118, 0x55, nullptr, rating(c.hit_size, 0.8f, 1.2f));                 // Hit Size:
    }
    Widget* name = widgetNew();
    addChild(panel, name);
    setRect(name, 0, 0x130, 0, 0);
    if (ch == 0)
        setLabel(name, 0x50, 0x20020);   // No Player
    else
        pcNameText(name, ch, 0xF8 - 56);
    Widget* prev = widgetNew();
    addChild(panel, prev);
    setRect(prev, 0, 0x130, 0, 0);
    setImage(prev, 0xD8, 0xDA, 0x100002);
    setHandlers(prev, [this, p](Widget&) { pcModeStep(p, -1); }, &Frontend::hitImage);   // 0x425B5C
    prev->user = p;
    Widget* next = widgetNew();
    addChild(panel, next);
    setRect(next, 0, 0x130, 0, 0);
    setImage(next, 0xD9, 0xDB, 0x100004);
    setHandlers(next, [this, p](Widget&) { pcModeStep(p, 1); }, &Frontend::hitImage);   // 0x425BAC
    next->user = p;
}

// 0x425B5C / 0x425BAC: the previous / next character (below 0: 6; player 1 skips No Player; after 6: Rogan for
// player 1, No Player for the others), then the screen again. pmextra: the Extra Characters 7..19 after the PC's 6
// (the wrap at the last of them instead of 6)
void Frontend::pcModeStep(int p, int dir)
{
    const int last = Chars::kPmCharCount - 1;
    if (dir < 0) {
        if (--pcChars[p] < 0 || ((p == 0 || charSel) && pcChars[p] == 0))   // (charsel: every seat a character)
            pcChars[p] = last;
    } else if (++pcChars[p] > last) {
        pcChars[p] = p == 0 || charSel ? 1 : 0;
    }
    if (pcSeats == 4)   // (No Player: nothing to be ready for; a computer seat is ready with any character)
        pcReady[p] = pcChars[p] != 0 && (pcCpu[p] || pcReady[p]);
    walking_ = false;
    screenPcMode();
}

// 0x4259AC (screen 16)
void Frontend::screenPcMode()
{
    walking_ = false;   // (called from a widget's handler: the walk stops, as the other screen builders' callers do)
    if (charSel) {   // charsel: the Character Select's cards (charSelectOpen)
        charSelectOpen(charSelPlayers);
        return;
    }
    if (style == kStyleRemake) {   // remakepages: the Remake layout (frontend_remake_pages.cpp)
        remakePcMode();
        return;
    }
    screen_ = 16;
    freeTree();
    Widget* root = widgetNew();
    root_ = root;
    setRect(root, 0, 0, 0, 0);
    setImage(root, 0xA4, -1, 0x20000A);
    if (pcSeats == 4) {   // ours (4P): a quarter panel per seat, P1 / P2 in the stones' top halves, P3 / P4 below
        for (int p = 0; p < 4; ++p) {
            Widget* cell = widgetNew();
            addChild(root, cell);
            setRect(cell, p % 2 == 0 ? 0x10 : 0x150, p < 2 ? 0x30 : 0xE6, 0x120, 0xB0);
            pcModePanel4(cell, p);
        }
    } else {
        for (int p = 0; p < 2; ++p) {
            Widget* panel = widgetNew();
            addChild(root, panel);
            setRect(panel, p == 0 ? 0x20 : 0x168, 0x40, 0xF8, 0x14A);
            pcModePanel(panel, p);
        }
    }
    Widget* title = widgetNew();
    addChild(root, title);
    setRect(title, 0, 0x10, 0, 0);
    setLabel(title, 2, 0x10020);   // "PC Mode", white 32, centred
    if (pcSeats == 4) {   // ours: who is still picking (the game starts when nobody is), in place of Start
        std::string wait;
        for (int p = 0; p < 4; ++p)
            if (pcChars[p] != 0 && !pcReady[p])
                wait += (wait.empty() ? "Waiting for P" : ", P") + std::to_string(p + 1);
        Widget* status = widgetNew();
        addChild(root, status);
        setRect(status, 0, 0x1A0, 0, 0);
        setText(status, wait.empty() ? std::string("Starting") : wait, 0x20020);
    } else {
        Widget* start = widgetNew();
        addChild(root, start);
        setRect(start, 0, 0x1A0, 0, 0);
        setLabel(start, 0x4F, 0x20020);   // "Start"
        setHandlers(start, [this](Widget&) {   // 0x425990: frontend_state = 5, the game starts
            walking_ = false;
            if (onPcModeStart)
                onPcModeStart();
        }, &Frontend::hitText);
    }
    Widget* ex = widgetNew();
    addChild(root, ex);
    setRect(ex, 0, 0x1A0 + spacing_, 0, 0);
    setLabel(ex, 0x24, 0x20020);   // "Exit"
    setHandlers(ex, [this](Widget&) {   // 0x4210F4: the main menu
        walking_ = false;
        screenMainMenu();
    }, &Frontend::hitText);
    ex->tag = 1;   // 0x427CB0: Escape
}

// ---- charsel: the Character Select of Arcade / Chapter Select (frontend.h charSelectOpen), the classic cards ----

// a card: "Player N" in the seat's colour, the portrait, the extras' lines, the name between the arrows (no ratings:
// the game keeps the arcade's rules)
void Frontend::charSelectPanel(Widget* panel, int p)
{
    if (pcChars[p] <= 0)
        pcChars[p] = p % 2 == 0 ? 1 : 2;
    const int ch = pcChars[p];
    Widget* who = widgetNew();
    addChild(panel, who);
    setRect(who, 0, 0x108, 0, 0);   // (under the portrait, where PC Mode has its ratings)
    setText(who, "Player " + std::to_string(p + 1), 0x20020);
    Widget* pic = widgetNew();
    addChild(panel, pic);
    setRect(pic, 0, 0, 0, 100);
    setImage(pic, pcPortraitId(ch, false), -1, 0x100020);
    const std::vector<std::string> notes = pcExtraNotes(ch);
    for (size_t i = 0; i < notes.size() && i < 1; ++i) {   // (the group only: "Plays as" is PC Mode's rules)
        Widget* n = widgetNew();
        addChild(panel, n);
        setRect(n, 0, 0x14A, 0, 0);
        setText(n, notes[i], 0x20020);
        n->draw = &Frontend::drawTextLit;
        n->fitW = 0xF8 - 40;
    }
    Widget* name = widgetNew();
    addChild(panel, name);
    setRect(name, 0, 0x130, 0, 0);
    pcNameText(name, ch, 0xF8 - 56);
    for (int d = 0; d < 2; ++d) {
        Widget* a = widgetNew();
        addChild(panel, a);
        setRect(a, 0, 0x130, 0, 0);
        setImage(a, d ? 0xD9 : 0xD8, d ? 0xDB : 0xDA, d ? 0x100004 : 0x100002);
        setHandlers(a, [this, p, d](Widget&) { pcModeStep(p, d ? 1 : -1); }, &Frontend::hitImage);
        a->user = p;
    }
}

void Frontend::charSelectOpen(int players)
{
    walking_ = false;
    charSel = true;
    charSelPlayers = std::clamp(players, 1, 2);   // (Players 3 / 4: P1 / P2 pick here; 3 / 4 keep Rogan 2 / G 2)
    pcSeats = 2;
    screen_ = 16;
    freeTree();
    Widget* root = widgetNew();
    root_ = root;
    setRect(root, 0, 0, 0, 0);
    setImage(root, 0xA4, -1, 0x20000A);
    for (int p = 0; p < charSelPlayers; ++p) {
        Widget* panel = widgetNew();
        addChild(root, panel);
        const int x = charSelPlayers == 1 ? (kWidth - 0xF8) / 2 : p == 0 ? 0x20 : 0x168;   // (one player: front and centre)
        setRect(panel, x, 0x30, 0xF8, 0x14A);
        charSelectPanel(panel, p);
    }
    Widget* title = widgetNew();
    addChild(root, title);
    setRect(title, 0, 0x10, 0, 0);
    setText(title, "Character Select", 0x10020);
    title->fitW = kWidth - 40;
    auto item = [&](int y, const std::string& text, int labelId, std::function<void()> fn) {
        Widget* w = widgetNew();
        addChild(root, w);
        setRect(w, 0, y, 0, 0);
        if (labelId >= 0)
            setLabel(w, labelId, 0x20020);
        else
            setText(w, text, 0x20020);
        setHandlers(w, [this, fn](Widget&) { walking_ = false; fn(); }, &Frontend::hitText);
        return w;
    };
    item(0x190, "", 0x4F, [this]() { if (onCharSelectStart) onCharSelectStart(false); });   // "Start"
    item(0x1AC, "Default Characters", -1, [this]() { if (onCharSelectStart) onCharSelectStart(true); });
    Widget* ex = item(0x1C8, "", 0x24, [this]() { screenMainMenu(); });   // "Exit"
    ex->tag = 1;   // (Escape)
}

// ---- remakepages: the Remake style's PC Mode (docs/menu_remake.md): the PC's tombstones (0xA4, their own art: the
// original's identity) centred on the Remake backdrop, the panels' texts in the Remake letters, the arrows as chevrons,
// Start / Exit as parchment strips, the prompts at the bottom right. The same seats, picks and actions ----
void Frontend::remakePcMode()
{
    remakePage(16, "pcmode" + std::to_string(pcSeats), true);
    rebuild_ = [this]() { remakePcMode(); };
    rGrid_ = true;
    rArt_ = 0xA4;
    const int ox = (cw_ - kWidth) / 2;   // (the PC's 640 x 480 layout on the tombstones, centred)
    if (pcSeats == 4) {
        for (int p = 0; p < 4; ++p)
            remakePcPanel4(p, ox + (p % 2 == 0 ? 0x10 : 0x150), p < 2 ? 0x30 : 0xE6);
    } else {
        for (int p = 0; p < 2; ++p)
            remakePcPanel(p, ox + (p == 0 ? 0x20 : 0x168), 0x40);
    }
    Widget* title = rWidget(0, 4, cw_, 44, &Frontend::drawRLabel, label(2));   // "PC Mode"
    title->user = 0xFFF2E6, title->seed = 37;
    Widget* start = nullptr;
    if (pcSeats == 4) {   // who is still picking (the game starts when nobody is), in place of Start
        std::string wait;
        for (int p = 0; p < 4; ++p)
            if (pcChars[p] != 0 && !pcReady[p])
                wait += (wait.empty() ? "Waiting for P" : ", P") + std::to_string(p + 1);
        Widget* status = rWidget(0, 402, cw_, 30, &Frontend::drawRLabel, wait.empty() ? std::string("Starting") : wait);
        status->user = 0xFFF2E6, status->seed = 41;
    } else {
        start = rStripButton(cw_ / 2 - 60, 404, 120, 26, label(0x4F), "Start the game with these characters", [this]() {
            if (onPcModeStart)   // (0x425990: frontend_state 5, the game starts)
                onPcModeStart();
        }, 310);
        rHome_ = start;
    }
    rStripButton(cw_ / 2 - 60, 436, 120, 26, label(0x24), "Back to the main menu (Escape)", [this]() { screenMainMenu(); }, 300);
    rHelp_[1] = 466;
    remakePrompts(root_, false, [this]() { screenMainMenu(); });
    if (remakeFirstFocus_ && !handShown_) {
        if (start)   // (the keyboard / a pad: on Start, as the PC's Enter)
            px_ = start->x + start->w / 2, py_ = start->y + start->h / 2;
        else
            remakeFocus(0);
    }
}

// a tombstone's panel (0x420080's places, the panel at x, y): the portrait, the five ratings, the name between the arrows
void Frontend::remakePcPanel(int p, int x, int y)
{
    const int ch = pcChars[p], pw = 0xF8;
    if (ch != 0) {
        const auto& c = Chars::pm_table(ch);
        Widget* pic = rWidget(x, y, pw, 100, &Frontend::drawRSprite);
        pic->id = int16_t(pcPortraitId(ch, false));
        const std::vector<std::string> notes = pcExtraNotes(ch);   // pmextra: between the portrait and the rows
        for (size_t i = 0; i < notes.size(); ++i) {
            Widget* n = rWidget(x + 4, y + 102 + int(i) * 24, pw - 8, 22, &Frontend::drawRLabel, notes[i]);
            n->user = i == 0 ? 0xE0C020 : 0xB0A898, n->seed = 70 + int(i);
        }
        const std::string hp = std::to_string(c.hp), rounds = std::to_string(c.rounds);
        const struct { int y, lid; std::string value; } rows[5] = {
            {0xB8, 0x51, hp}, {0xD0, 0x52, rounds}, {0xE8, 0x53, label(rating(c.damage, 0.8f, 1.2f))},
            {0x100, 0x54, label(rating(float(c.reload_ms), 200.0f, 300.0f))}, {0x118, 0x55, label(rating(c.hit_size, 0.8f, 1.2f))}};
        for (const auto& r : rows) {
            Widget* s = rWidget(x + 4, y + r.y, pw - 8, 22, &Frontend::drawRStat, label(r.lid));
            s->value = r.value;
            s->user = 16;
            s->seed = 600 + r.y;
        }
    }
    Widget* name = rWidget(x + 28, y + 0x130 - 2, pw - 56, 30, &Frontend::drawRLabel, ch == 0 ? std::string(label(0x50)) : Chars::pm_table(ch).name);
    name->user = 0xFFF2E6, name->seed = 43 + p;
    name->fitW = pw - 56 - 24;   // namefit: a long name kept off the arrows (pmpick: a clear gap, 12 each side)
    for (int d = 0; d < 2; ++d) {   // 0x425B5C / 0x425BAC
        Widget* a = rWidget(d ? x + pw - 30 : x, y + 0x130 - 2, 30, 28, &Frontend::drawRSide);
        a->user = d;
        a->help = std::string(d ? "The next character" : "The character before") + " for player " + std::to_string(p + 1);
        setHandlers(a, [this, p, d](Widget&) { pcModeStep(p, d ? 1 : -1); }, &Frontend::hitRect);
        a->nav = true;
    }
}

// 4P: a seat's quarter panel (pcModePanel4's places): its colour frame, the half-size portrait, "Player N", the name
// between the arrows, Ready (or CPU), the five ratings
void Frontend::remakePcPanel4(int p, int x, int y)
{
    static const int kColour[4] = {0xD02020, 0x2858E0, 0x20B030, 0xE0C020};   // P1 red, P2 blue, P3 green, P4 yellow
    const int ch = pcChars[p];
    const bool locked = pcReady[p] && !pcCpu[p];
    const bool extra = Chars::pm_extra(ch) != nullptr;   // pmextra: "Player N - Extra" in yellow
    Widget* who = rWidget(x + 0x58, y + 3, 0xC4, 24, &Frontend::drawRLabel,
                          "Player " + std::to_string(p + 1) + (extra ? " - Extra" : ""));
    who->user = extra ? 0xE0C020 : 0xFFF2E6, who->seed = 51 + p;
    Widget* name = rWidget(x + 0x58 + 26, y + 0x1C, 0xC4 - 52, 24, &Frontend::drawRLabel, ch == 0 ? std::string(label(0x50)) : Chars::pm_table(ch).name);
    name->user = 0xFFF2E6, name->seed = 55 + p;
    name->fitW = 0xC4 - 52 - 24;   // namefit: a long name kept off the arrows (pmpick: a clear gap, 12 each side)
    for (int d = 0; d < 2; ++d) {
        Widget* a = rWidget(d ? x + 0x58 + 0xC4 - 26 : x + 0x58, y + 0x1C, 26, 24, &Frontend::drawRSide);
        a->user = d;
        a->seed = locked ? 1 : 0;
        a->help = std::string(d ? "The next character" : "The character before") + " for player " + std::to_string(p + 1);
        setHandlers(a, [this, p, d](Widget&) { pcModeSeat(p, d ? kPcNext : kPcPrev); }, &Frontend::hitRect);
        a->nav = true;
    }
    if (ch != 0) {
        const auto& c = Chars::pm_table(ch);
        const int bx = p == 0 ? x + 0x58 + 0xC4 / 2 - 44 : x + 0x58 + 6;   // (players 2-4: Ready / CPU, then the toggle)
        if (pcCpu[p]) {
            Widget* cpu = rWidget(bx, y + 0x36, 88, 22, &Frontend::drawRLabel, "CPU");
            cpu->user = 0x8A827A, cpu->seed = 59;
        } else {
            Widget* ready = rStripButton(bx, y + 0x36, 88, 20, pcReady[p] ? "Ready!" : "Ready",
                                         pcReady[p] ? "Ready: wait for the others (click: not ready)" : "Ready to start",
                                         [this, p]() { pcModeSeat(p, kPcReady); }, 320 + p);
            ready->nav = true;
        }
        if (p > 0) {
            Widget* t = rStripButton(x + 0x58 + 0xC4 - 6 - 88, y + 0x36, 88, 20, pcCpu[p] ? "Player" : "CPU",
                                     pcCpu[p] ? "A person plays this seat" : "A computer teammate plays this seat",
                                     [this, p]() { pcModeSeat(p, kPcCpu); }, 330 + p);
            t->nav = true;
        }
        const std::string hp = std::to_string(c.hp), rounds = std::to_string(c.rounds);
        const struct { int y, lid; std::string value; } rows[5] = {
            {0x4E, 0x51, hp}, {0x60, 0x52, rounds}, {0x72, 0x53, label(rating(c.damage, 0.8f, 1.2f))},
            {0x84, 0x54, label(rating(float(c.reload_ms), 200.0f, 300.0f))}, {0x96, 0x55, label(rating(c.hit_size, 0.8f, 1.2f))}};
        for (const auto& r : rows) {
            Widget* s = rWidget(x + 10, y + r.y, 0x10C, 18, &Frontend::drawRStat, label(r.lid));
            s->value = r.value;
            s->user = 12;
            s->seed = 640 + r.y;
        }
        Widget* pic = rWidget(x + 4, y + 4, 0x52, 0x50, &Frontend::drawRSprite);
        pic->id = int16_t(pcPortraitId(ch, false));
        pic->user = 1;
    }
    Widget* frame = rWidget(x, y, 0x120, 0xB0, &Frontend::drawRFrame);   // (added last: drawn first, under the rest)
    frame->user = kColour[p];
}

// ---- ours (4P, docs/pcmode.md "4 players in PC Mode"): four panels, a pick and Ready per seat ----

void Frontend::pcModeOpen(int players, const bool cpu[4], uint32_t seed)
{
    charSel = false;
    pcSeats = players >= 3 ? 4 : 2;
    if (pcSeats == 4) {
        static const int kDefault[4] = {1, 2, 3, 4};   // Rogan, G, Rogan 2, G 2 (the 4P mode's bodies, docs/coop4.md)
        uint32_t r = seed | 1;
        for (int p = 0; p < 4; ++p) {
            pcReady[p] = false;
            pcCpu[p] = p > 0 && cpu && cpu[p];
            if (pcCpu[p]) {   // a computer teammate: a random character, ready (the arrows still change it)
                r = r * 1103515245u + 12345u;
                pcChars[p] = 1 + int((r >> 16) % 6);
                pcReady[p] = true;
            } else if (p < players && pcChars[p] == 0) {
                pcChars[p] = kDefault[p];
            }
        }
        if (pcChars[0] == 0)
            pcChars[0] = 1;
    }
    screenPcMode();
}

bool Frontend::pcModeAllReady() const
{
    if (pcSeats != 4 || pcChars[0] == 0)
        return false;
    for (int p = 0; p < 4; ++p)
        if (pcChars[p] != 0 && !pcReady[p])
            return false;
    return true;
}

void Frontend::pcModeSeat(int seat, int act)
{
    if (pcSeats != 4 || seat < 0 || seat > 3 || screen_ != 16)
        return;
    if (act == kPcPrev || act == kPcNext) {
        if (pcReady[seat] && !pcCpu[seat])
            return;   // (Ready: the pick is kept until the player takes Ready back)
        pcModeStep(seat, act == kPcPrev ? -1 : 1);
    } else if (act == kPcCpu) {
        if (seat == 0 || pcChars[seat] == 0)
            return;
        pcCpu[seat] = !pcCpu[seat];
        pcReady[seat] = pcCpu[seat];
        walking_ = false;
        screenPcMode();
    } else {
        if (pcCpu[seat] || pcChars[seat] == 0)
            return;
        pcReady[seat] = act == kPcReady ? !pcReady[seat] : false;
        walking_ = false;
        screenPcMode();
    }
    if (pcModeAllReady() && onPcModeStart) {
        walking_ = false;
        onPcModeStart();
    }
}

// a quarter panel (288 x 176): the seat's colour frame, the portrait (+0x18) at half size at the top left, "Player N",
// the name between the arrows and Ready (or CPU) beside it, the five ratings of 0x420080 under them
void Frontend::pcModePanel4(Widget* cell, int p)
{
    static const int kColour[4] = {0xD02020, 0x2858E0, 0x20B030, 0xE0C020};   // P1 red, P2 blue, P3 green, P4 yellow
    const int ch = pcChars[p];
    Widget* frame = widgetNew();
    addChild(cell, frame);
    setRect(frame, 0, 0, 0, 0);
    frame->draw = &Frontend::drawFrame;
    frame->user = kColour[p];
    Widget* who = widgetNew();
    addChild(cell, who);
    setRect(who, 0x58, 4, 0xC4, 0);
    const bool extra = Chars::pm_extra(ch) != nullptr;   // pmextra: "Player N - Extra" in yellow
    setText(who, "Player " + std::to_string(p + 1) + (extra ? " - Extra" : ""), 0x20020);
    if (extra)
        who->draw = &Frontend::drawTextLit;
    if (ch != 0) {
        const auto& c = Chars::pm_table(ch);
        Widget* pic = widgetNew();
        addChild(cell, pic);
        setRect(pic, 4, 4, 0x52, 0x50);
        setImage(pic, pcPortraitId(ch, false), -1, 0x100020);
        pic->draw = &Frontend::drawHalfImage;
        auto row = [&](int y, int labelId, const std::string* text, int valueLabel) {
            Widget* l = widgetNew();
            addChild(cell, l);
            setRect(l, 10, y, 0x10C, 0);
            setLabel(l, labelId, 0x20002);
            Widget* v = widgetNew();
            addChild(cell, v);
            setRect(v, 10, y, 0x10C, 0);
            if (text)
                setText(v, *text, 0x20004);
            else
                setLabel(v, valueLabel, 0x20004);
        };
        const std::string hp = std::to_string(c.hp), rounds = std::to_string(c.rounds);
        row(0x4E, 0x51, &hp, 0);
        row(0x60, 0x52, &rounds, 0);
        row(0x72, 0x53, nullptr, rating(c.damage, 0.8f, 1.2f));
        row(0x84, 0x54, nullptr, rating(float(c.reload_ms), 200.0f, 300.0f));
        row(0x96, 0x55, nullptr, rating(c.hit_size, 0.8f, 1.2f));
        Widget* ready = widgetNew();
        addChild(cell, ready);
        setRect(ready, 0x58, 0x34, p == 0 ? 0xC4 : 0x62, 0);
        if (p > 0) {   // players 2-4: a person / a CPU teammate (the right half)
            Widget* t = widgetNew();
            addChild(cell, t);
            setRect(t, 0x58 + 0x62, 0x34, 0x62, 0);
            setText(t, pcCpu[p] ? "Player" : "CPU", 0x20020);
            setHandlers(t, [this, p](Widget&) { pcModeSeat(p, kPcCpu); }, &Frontend::hitText);
        }
        if (pcCpu[p]) {
            setText(ready, "CPU", 0xA0020);   // grey 24
        } else {
            setText(ready, pcReady[p] ? "Ready!" : "Ready", 0x20020);
            if (pcReady[p])
                ready->draw = &Frontend::drawTextLit;   // yellow while ready
            setHandlers(ready, [this, p](Widget&) { pcModeSeat(p, kPcReady); }, &Frontend::hitText);
        }
    }
    Widget* name = widgetNew();
    addChild(cell, name);
    setRect(name, 0x58, 0x1C, 0xC4, 0);
    if (ch == 0)
        setLabel(name, 0x50, 0x20020);   // No Player
    else
        pcNameText(name, ch, 0xC4 - 52);
    const bool locked = pcReady[p] && !pcCpu[p];
    Widget* prev = widgetNew();
    addChild(cell, prev);
    setRect(prev, 0x58, 0x1C, 0xC4, 0);
    setImage(prev, 0xD8, locked ? -1 : 0xDA, 0x100002);
    setHandlers(prev, [this, p](Widget&) { pcModeSeat(p, kPcPrev); }, &Frontend::hitImage);
    Widget* next = widgetNew();
    addChild(cell, next);
    setRect(next, 0x58, 0x1C, 0xC4, 0);
    setImage(next, 0xD9, locked ? -1 : 0xDB, 0x100004);
    setHandlers(next, [this, p](Widget&) { pcModeSeat(p, kPcNext); }, &Frontend::hitImage);
}

void Frontend::drawHalfImage(Widget& w)
{
    int x, y, ww, hh;
    absRect(w, x, y, ww, hh);
    const int iw = spr_.width(w.id), ih = spr_.height(w.id);
    const std::vector<uint8_t> img = spr_.image(w.id);
    if (img.empty())
        return;
    const int ow = iw / 2, oh = ih / 2;
    if (w.flags & 0x20)
        x += (ww - ow) / 2;
    if (w.flags & 0x40)
        y += (hh - oh) / 2;
    for (int j = 0; j < oh; ++j) {
        const int dy = y + j;
        if (dy < 0 || dy >= kHeight)
            continue;
        for (int i = 0; i < ow; ++i) {
            const int dx = x + i;
            if (dx < 0 || dx >= cw_)
                continue;
            int sum[3] = {}, a = 0;
            for (int k = 0; k < 4; ++k) {
                const uint8_t* s = &img[(size_t(j * 2 + k / 2) * iw + size_t(i * 2 + k % 2)) * 4];
                for (int c = 0; c < 3; ++c)
                    sum[c] += s[c] * s[3];
                a += s[3];
            }
            if (a == 0)
                continue;
            uint8_t* d = &(*canvas_)[(size_t(dy) * cw_ + size_t(dx)) * 4];
            const int cover = a / 4;   // 0..255
            for (int c = 0; c < 3; ++c)
                d[c] = uint8_t((d[c] * (255 - cover) + (sum[c] / a) * cover) / 255);
            d[3] = 255;
        }
    }
}

void Frontend::drawFrame(Widget& w)
{
    int x, y, ww, hh;
    absRect(w, x, y, ww, hh);
    const uint8_t rgb[3] = {uint8_t(w.user >> 16), uint8_t(w.user >> 8), uint8_t(w.user)};
    for (int j = std::max(0, y); j < std::min(kHeight, y + hh); ++j)
        for (int i = std::max(0, x); i < std::min(cw_, x + ww); ++i) {
            if (j >= y + 2 && j < y + hh - 2 && i >= x + 2 && i < x + ww - 2)
                continue;
            uint8_t* d = &(*canvas_)[(size_t(j) * cw_ + size_t(i)) * 4];
            d[0] = rgb[0], d[1] = rgb[1], d[2] = rgb[2], d[3] = 255;
        }
}

// screen 9's rows (THOTD.EXE screen_high_scores_pc_mode 0x4234E0): four columns 0x50 wide from y 0x60, rows 0x1A apart,
// at x 0x40 (the name, left), 0xD0 (the stage, right), 0x160 (the score "%d", right) and 0x1F0 (the character's
// name, right, with its small picture +0x1C, 0xF3..0xF8). Ours: the picture right of the name instead of under it;
// pmpick (user 2026-10-30: long names still touched the score): the character's column 0x10 further right and its
// names kept 0x20 off the score's column (a clear gap, every language)
void Frontend::pcModeRankColumns(Widget* root, const std::vector<RankRow>& rows)
{
    const int colX[4] = {0x40, 0xD0, 0x160, 0x200};
    for (int c = 0; c < 4; ++c) {
        Widget* col = widgetNew();
        addChild(root, col);
        setRect(col, colX[c], 0x60, 0x50, 0x190);
        for (int i = 0; i < 10 && size_t(i) < rows.size(); ++i) {
            const RankRow& r = rows[size_t(i)];
            Widget* cell = widgetNew();
            addChild(col, cell);
            if (c < 3) {
                setRect(cell, 0, i * 0x1A, 0x50, 0x1A);
                setText(cell, c == 0 ? r.name : c == 1 ? r.stage : std::to_string(r.score), c == 0 ? 0x20002 : 0x20004);
                cell->ttfScale = 0.8f;   // sweep2: the language's font (taller than the PC's) kept inside the 0x1A rows
                cell->fitW = 0x50 + 0x30;   // (into the gap left of the column at most)
                continue;
            }
            const auto& ch = Chars::pm_table(r.ch);
            const int sp2 = pcPortraitId(r.ch, true), w = spr_.width(sp2);
            setRect(cell, 0, i * 0x1A, 0x50 - (w > 0 ? w + 4 : 0), 0x1A);
            const std::string nm = r.ch == 0 ? std::string("No Player") : std::string(ch.name);
            const int room = 0x50 - (w > 0 ? w + 4 : 0) + 0x30;
            int nw = 0, nh = 0;
            measure(nm, 0x20004, nw, nh);
            // pmextra: an Extra Character's long name ("Researcher (woman)") in the PC's small font (its picture: the
            // small picture of the PC character it plays as)
            const std::string shown = langOn() ? lang::tr(nm) : nm;
            const bool small = nw > room && !lang::fontFor(shown);
            // namefit: still too wide in the small font: shortPcName's short name, never into the score's column
            const std::string fit = !small ? nm : shortPcName(nm, shown, langOn(), [&](const std::string& t) {
                int tw = 0, th = 0;
                measureRaw(t, 0x800004, tw, th);
                return tw <= room;
            });
            setText(cell, fit, small ? 0x800004 : 0x20004);
            if (small)
                cell->y += 7;
            cell->ttfScale = 0.8f;
            cell->fitW = room;
            if (w > 0) {
                Widget* pic = widgetNew();
                addChild(col, pic);
                setRect(pic, 0x50 - w, i * 0x1A, w, 0x1A);
                setImage(pic, sp2, -1, 0x100004);
            }
        }
    }
}

} // namespace hotd::game
