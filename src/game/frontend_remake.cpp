// The menu's Remake style (remakemenu; frontend.h "the menu's two styles", docs/menu_remake.md): the main menu, Quit's
// question, Options and the settings pages laid out across a wide screen in the manner of The House of the Dead:
// Remake (2022): the game's logo at the top centre, a dark torn panel with a red glow at its bottom holding the items
// as torn parchment strips with red grungy capitals, the settings as parchment tabs over a wide torn panel with the
// rows (label left, value right: a chevron for a choice, a bloody handprint for a switch), the controls prompts at the
// bottom right over red claw marks, the bony hand for the mouse. Only the layout and the behaviour are the Remake's:
// every picture is the player's own PC files (the hall 0xD1, the logo 0xD4, the fonts, the hand) or drawn here
// (noise-torn panels and paper, blood, claw marks, handprints). The items, rows and actions are the Classic style's
// (mainMenuItems, optionsRows, extras): one model, two layouts.
//
// Units: a Remake screen is 480 units high and cw_ wide (setViewAspect); the picture has ps_ (2 .. 3, setOutputHeight) pixels a unit.
#include "game/frontend.h"
#include "game/keybinds.h"
#include "game/pad_prompts.h"
#include "game/lang.h"
#include "game/player_colours.h"
#include "util/png_read.h"
#include "util/utf8.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <thread>

namespace hotd::game {

namespace {

uint32_t hash3(int x, int y, int s)
{
    uint32_t h = uint32_t(x) * 374761393u + uint32_t(y) * 668265263u + uint32_t(s) * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
float rnd(int x, int y, int s) { return float(hash3(x, y, s) & 0xFFFFFF) / float(0x1000000); }
// value noise in 0..1 (smooth between the integer points)
float vnoise(float x, float y, int s)
{
    const float fx = std::floor(x), fy = std::floor(y);
    const int ix = int(fx), iy = int(fy);
    float tx = x - fx, ty = y - fy;
    tx = tx * tx * (3 - 2 * tx), ty = ty * ty * (3 - 2 * ty);
    const float a = rnd(ix, iy, s), b = rnd(ix + 1, iy, s), c = rnd(ix, iy + 1, s), d = rnd(ix + 1, iy + 1, s);
    return (a + (b - a) * tx) * (1 - ty) + (c + (d - c) * tx) * ty;
}
float fbm(float x, float y, int s)
{
    return (vnoise(x, y, s) * 0.55f + vnoise(x * 2.1f, y * 2.1f, s + 17) * 0.3f + vnoise(x * 4.3f, y * 4.3f, s + 31) * 0.15f);
}
float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
// menuperf: f(y) for the rows y0 .. y1 - 1 of a picture, on up to 4 threads when it is big enough (each row written
// by one thread only, so the same pixels; the paper strips' / torn panels' noise: 6-40 ms on one thread at 1080 lines)
// (the helpers wait between jobs: a job costs no thread start; the menu's thread works on it too)
class RowPool {
public:
    static RowPool& get()
    {
        static RowPool p;
        return p;
    }
    unsigned helpers() const { return unsigned(threads_.size()); }
    void run(int y0, int y1, const std::function<void(int)>& f)
    {
        {
            std::lock_guard<std::mutex> lk(m_);
            f_ = &f, end_ = y1, next_ = y0, busy_ = int(threads_.size());
            ++gen_;
        }
        cv_.notify_all();
        work();
        std::unique_lock<std::mutex> lk(m_);
        done_.wait(lk, [this]() { return busy_ == 0; });
        f_ = nullptr;
    }
    ~RowPool()
    {
        {
            std::lock_guard<std::mutex> lk(m_);
            stop_ = true;
        }
        cv_.notify_all();
        for (std::thread& t : threads_) t.join();
    }

private:
    RowPool()
    {
        const unsigned n = std::min(3u, std::max(1u, std::thread::hardware_concurrency()) - 1);
        for (unsigned i = 0; i < n; ++i)
            threads_.emplace_back([this]() {
                uint64_t seen = 0;
                for (;;) {
                    {
                        std::unique_lock<std::mutex> lk(m_);
                        cv_.wait(lk, [&]() { return stop_ || gen_ != seen; });
                        if (stop_) return;
                        seen = gen_;
                    }
                    work();
                    std::lock_guard<std::mutex> lk(m_);
                    if (--busy_ == 0) done_.notify_one();
                }
            });
    }
    void work()
    {
        for (int y; (y = next_.fetch_add(4)) < end_;)
            for (int k = y; k < std::min(y + 4, end_); ++k) (*f_)(k);
    }
    std::vector<std::thread> threads_;
    std::mutex m_;
    std::condition_variable cv_, done_;
    const std::function<void(int)>* f_ = nullptr;
    std::atomic<int> next_{0};
    int end_ = 0, busy_ = 0;
    uint64_t gen_ = 0;
    bool stop_ = false;
};
template <class F> void forRows(int y0, int y1, size_t pixels, const F& f)
{
    if (pixels < 20000 || y1 - y0 < 16 || RowPool::get().helpers() == 0) {
        for (int y = y0; y < y1; ++y) f(y);
        return;
    }
    const std::function<void(int)> fn = [&f](int y) { f(y); };
    RowPool::get().run(y0, y1, fn);
}
uint32_t mix(uint32_t a, uint32_t b, float t)
{
    t = clamp01(t);
    auto ch = [&](int sh) { return uint32_t(float((a >> sh) & 255) * (1 - t) + float((b >> sh) & 255) * t + 0.5f) << sh; };
    return ch(16) | ch(8) | ch(0);
}
uint32_t scale(uint32_t c, float f)
{
    auto ch = [&](int sh) { return uint32_t(std::clamp(float((c >> sh) & 255) * f, 0.0f, 255.0f)) << sh; };
    return ch(16) | ch(8) | ch(0);
}
std::string upper(std::string s)
{
    for (char& c : s)
        if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
    return s;
}
// upper() of the PC fonts' bytes (Latin-1): the accented small letters too (à..þ but ÷; ÿ has no capital there), so
// the small capitals of Spanish / Portuguese / French ... match their letters (2026-11-03). Never on UTF-8
std::string upperPc(std::string s)
{
    for (char& c : s) {
        const uint8_t b = uint8_t(c);
        if (b >= 'a' && b <= 'z') c = char(b - 'a' + 'A');
        else if (b >= 0xE0 && b <= 0xFE && b != 0xF7) c = char(b - 0x20);
    }
    return s;
}

// the colours
constexpr uint32_t kInk = 0xA81410;       // the strips' red letters
constexpr uint32_t kInkLit = 0xE0261C;    // ... on the strip under the hand / the focus
constexpr uint32_t kInkGrey = 0x5E5650;   // ... on a grey (not available) strip
constexpr uint32_t kRowText = 0xC4BDB5;   // the settings' labels / values
constexpr uint32_t kRowLit = 0xFFF2E6;
constexpr uint32_t kRowOff = 0x6A625C;
constexpr uint32_t kTabText = 0x3A2620;   // a tab's dark letters (the current one: kInk)
// the prompts (units): the key caps' size (a pad's ring: kPromptCapH across), the gap to the label, the screen's
// right / bottom margin, a row's step
constexpr float kPromptCapW = 30.0f, kPromptCapH = 18.0f, kPromptGap = 9.0f;
constexpr int kPromptMargin = 18, kPromptStep = 26;

} // namespace

// ---- the style, the screen's width, testing ----
// lang: the drawn texts and pieces redrawn, the screen rebuilt (its layout measures the texts)
void Frontend::langChanged()
{
    tiles_.clear();
    staticSig_.clear();
    fgSig_.clear();
    fontColour_.clear();
    reFile_.key.clear();
    reList_.key.clear();
    const int s = style;
    style = -1;
    setStyle(s);
}

void Frontend::setStyle(int s)
{
    if (s == style)
        return;
    style = s;
    switch (screen_) {   // the screens with both layouts: rebuilt in the new one
    case 1: screenMainMenu(); break;
    case 2: screenOptions(); break;
    case 21: screenQuit(); break;
    case 22: screenPlayers(); break;   // (playerpick)
    case 100: screenExtras(); break;
    case 7: screenBossMode(); break;   // remakepages: these pages have both layouts too
    case 8: screenRanking(); break;
    case 9: screenChapters(); break;
    case 16: screenPcMode(); break;
    case 17: screenGallery(); break;
    case 19: screenScenes(); break;
    case 20: screenJukebox(); break;
    case 32: case 33: case 34: case 35: case 36: screenBossTimes(screen_ - 30); break;
    default: break;
    }
}

void Frontend::setViewAspect(float aspect)
{
    if (!(aspect > 0))
        return;
    int w = int(std::lround(480.0f * std::clamp(aspect, 4.0f / 3.0f, 2.5f))) & ~1;
    w = std::clamp(w, kWidth, 1200);
    if (w == wideW_)
        return;
    const int old = wideW_;
    wideW_ = w;
    if (sc_ > 1 && rebuild_) {   // the Remake screen shown: laid out again (the pointer kept in place, scaled)
        px_ = px_ * w / std::max(1, old);
        const auto f = rebuild_;
        f();
    }
}

bool Frontend::widgetCentre(const std::string& text, int& x, int& y)
{
    bool found = false;
    std::function<void(Widget*, int, int, int, int)> walk = [&](Widget* w, int ox, int oy, int pw, int ph) {
        for (; w && !found; w = w->next) {
            const int ax = ox + w->x, ay = oy + w->y, aw = w->w ? w->w : pw, ah = w->h ? w->h : ph;
            const std::string t = (w->flags & 0x1000) ? label(w->id) : w->text;
            if (t == text && w->activate) {
                x = ax + aw / 2, y = ay + ah / 2;
                found = true;
                return;
            }
            walk(w->child, ax, ay, aw, ah);
        }
    };
    walk(root_, 0, 0, cw_, kHeight);
    return found;
}

std::string Frontend::focusText() const
{
    for (const Widget* w = root_ && sc_ > 1 ? root_->child : nullptr; w; w = w->next)
        if (w->nav && px_ >= w->x && px_ < w->x + w->w && py_ >= w->y && py_ < w->y + w->h)
            return w->text;
    return "";
}

// ---- a Remake screen's tree ----
void Frontend::remakeBegin(int screen, const std::string& key)
{
    screen_ = screen;
    freeTree();
    texts_.clear();
    cw_ = wideW_;
    sc_ = 2;
    fadeReset_ = true;   // (a new screen: shown)
    ++treeGen_;   // (the frame's widget layer: drawn again)
    remakeFirstFocus_ = key != remakeKey_;
    remakeKey_ = key;
    rPlain_ = rGrid_ = false, rTabFn_ = nullptr, rPage_[2] = 0, rArt_ = -1;   // (remakepages: a page sets its own)
    rTabsRtl_ = false;
    if (key != ddKey_)
        ddRow_ = -1;   // (remakefix: another page: its dropdown closed)
    root_ = widgetNew();
    setRect(root_, 0, 0, 0, 0);
    root_->draw = &Frontend::drawRPanel;
    root_->flags |= 0x100;
    rHelp_[0] = cw_ / 2, rHelp_[1] = 458, rHelp_[2] = cw_ - 300;
}

// the main menu: the logo at the top centre, the panel under it with the items as strips (the same items, actions,
// helps and Escape item as the Classic style's)
void Frontend::remakeMainMenu()
{
    const int grp = debugItems_ ? 0 : mainGroup_;
    remakeBegin(1, "main" + std::to_string(grp) + (debugItems_ ? "d" : ""));
    idleReset_ = true;   // 0x421119: the idle timer starts with the screen
    rebuild_ = [this]() { remakeMainMenu(); };
    root_->user = 0;
    const std::vector<MainItem> items = mainMenuItems();
    const int n = int(items.size());
    size_t mainCount = 0;
    while (mainCount < items.size() && !items[mainCount].debug)
        ++mainCount;
    const int pw = 290, ph = 304, px = (cw_ - pw) / 2, py = 150;
    rPanel_[0] = px, rPanel_[1] = py, rPanel_[2] = pw, rPanel_[3] = ph;
    const int top = py + 30, bottom = py + ph - 22;
    const int step = std::min(32, (bottom - top) / std::max(1, n));
    const int sh = std::min(26, step - 4);
    // polish (user 2026-11-03: "Survival Arena: Chapter 1: The Courtyard" ran past its strip): the strips as wide as the
    // longest label needs, up to the panel's inside (drawRStrip then makes a label still too long smaller)
    int sw = 212;
    for (const MainItem& it : items)
        if (!it.heading)
            sw = std::max(sw, int(std::ceil(rTextWidth(it.name(), std::min(15.0f, float(sh) * 0.6f)))) + 34);
    sw = std::min(sw, pw - 24);
    for (int k = 0; k < n; ++k) {
        const MainItem& it = items[size_t(k)];
        Widget* w = widgetNew();
        addChild(root_, w);
        setRect(w, cw_ / 2 - sw / 2, top + k * step, sw, sh);   // (one column on the panel's centre)
        w->text = it.name();
        w->value = it.tag;   // (polish: Survival's small red line under its name)
        w->help = it.help;
        w->seed = 101 + k * 7 + grp * 53;
        w->draw = &Frontend::drawRStrip;
        w->flags |= 0x100;
        w->user = it.heading ? 2 : it.act ? 0 : 1;
        if (it.act) {
            setHandlers(w, [this, act = it.act](Widget&) {
                walking_ = false;   // 0x42792C before a new screen
                act();
            }, &Frontend::hitRect);
            w->nav = true;
        }
        if (size_t(k) + 1 == mainCount)
            w->tag = 1;   // 0x427CB0: Escape activates Quit (a group: Back)
    }
    remakePrompts(root_, false, [this]() { escapeKey(); });
    addVersionTag();
    if (remakeFirstFocus_ && !handShown_)
        remakeFocus(0);
}

// Quit's question (the Classic's screen 21: "Are you sure?", Yes / No with the tags 2 / 1)
void Frontend::remakeQuit()
{
    remakeBegin(21, "quit");
    rebuild_ = [this]() { remakeQuit(); };
    root_->user = 2;
    const int pw = 290, ph = 170, px = (cw_ - pw) / 2, py = 170;
    rPanel_[0] = px, rPanel_[1] = py, rPanel_[2] = pw, rPanel_[3] = ph;
    Widget* q = widgetNew();
    addChild(root_, q);
    setRect(q, px, py + 26, pw, 30);
    q->text = label(8);   // "Are you sure?"
    q->draw = &Frontend::drawRLabel;
    q->flags |= 0x100;
    q->user = int(kRowLit), q->seed = 17;
    for (int k = 0; k < 2; ++k) {
        Widget* w = widgetNew();
        addChild(root_, w);
        setRect(w, cw_ / 2 - 80, py + 72 + k * 34, 160, 26);
        w->text = label(k ? 10 : 9);
        w->help = k ? "Back to the main menu (Escape)" : "Leave the game";
        w->seed = 300 + k * 11;
        w->draw = &Frontend::drawRStrip;
        w->flags |= 0x100;
        w->nav = true;
        w->tag = k ? 1 : 2;
        if (k)
            setHandlers(w, [this](Widget&) { walking_ = false; screenMainMenu(); }, &Frontend::hitRect);
        else
            setHandlers(w, [this](Widget&) { walking_ = false; if (onMainMenu) onMainMenu(5); }, &Frontend::hitRect);
    }
    rHelp_[1] = py + ph + 14;
    remakePrompts(root_, false, [this]() { screenMainMenu(); });
    if (remakeFirstFocus_ && !handShown_) {   // (the focus on No, as Escape)
        remakeFocus(0);
        remakeFocus(1);
    }
}

// playerpick: the players' quick choice (the Classic's screen 22) on a torn panel: the game's name, "Players" and the
// numbers as big parchment strips (the chosen one lit), the 4 players mode's tag, a strip per seat (Human / CPU),
// Start and Back; the focus (and the hand) on Start when it opens
void Frontend::remakePlayers()
{
    remakeBegin(22, "players");
    rebuild_ = [this]() { remakePlayers(); };
    root_->user = 3;
    rPlain_ = true;   // (the live attract untinted behind it, as the main menu's)
    rGrid_ = true;    // (the arrows: the nearest strip that way)
    const int pw = 400, px = (cw_ - pw) / 2, py = 104;
    auto add = [&](int x, int y, int w, int h, void (Frontend::*draw)(Widget&), const std::string& text) {
        Widget* wd = widgetNew();
        addChild(root_, wd);
        setRect(wd, x, y, w, h);
        wd->draw = draw;
        wd->flags |= 0x100;
        wd->text = text;
        return wd;
    };
    auto button = [&](Widget* w, std::function<void()> act) {
        setHandlers(w, [act](Widget&) { act(); }, &Frontend::hitRect);
        w->nav = true;
    };
    Widget* title = add(px, py + 16, pw, 32, &Frontend::drawRLabel, pickMode_);
    title->user = int(kRowLit), title->seed = 17;
    Widget* lab = add(px + 12, py + 66, 126, 34, &Frontend::drawRLabel, "Players");
    lab->user = int(kRowText), lab->seed = 19;
    for (int k = 1; k <= 4; ++k) {
        Widget* b = add(px + 146 + (k - 1) * 62, py + 60, 52, 42, &Frontend::drawRStrip, std::to_string(k));
        b->user = k == pickN_ ? 4 : 3;   // (big; 4: the chosen one)
        b->seed = 700 + k * 7;
        b->help = k == 1 ? "1 player" : k == 2 ? "2 players (player 2: a person or the computer)"
                                               : std::to_string(k) + " players: the 4 players mode (players 2-4 join with their trigger)";
        button(b, [this, k]() { pickPlayers(k); });
    }
    int y = py + 112;
    // (Light Gun Edition: no "Experimental" tag for the 4 players mode)
    y += 22;
    for (int p = 1; p < pickN_; ++p) {
        Widget* s = add(cw_ / 2 - 130, y, 260, 28, &Frontend::drawRStrip, pickSeatText(p));
        s->seed = 760 + p * 11;
        s->help = "Player " + std::to_string(p + 1) + ": you or the computer (CPU level: Options > Co-op)";
        button(s, [this, p]() { pickSeat(p); });
        y += 34;
    }
    y += 10;
    Widget* start = add(cw_ / 2 - 158, y, 150, 32, &Frontend::drawRStrip, "Start");
    start->seed = 801;
    start->tag = 2;
    start->help = "Start " + pickMode_ + " with " + std::to_string(pickN_) + (pickN_ == 1 ? " player" : " players");
    button(start, [this]() { pickStart(); });
    Widget* back = add(cw_ / 2 + 8, y, 150, 32, &Frontend::drawRStrip, "Back");
    back->seed = 811;
    back->help = "Back (Escape)";
    auto goBack = [this]() {
        walking_ = false;
        if (pickBack_) {
            const auto f = pickBack_;
            f();
        } else
            screenMainMenu();
    };
    button(back, goBack);
    const int ph = y + 32 + 24 - py;
    rPanel_[0] = px, rPanel_[1] = py, rPanel_[2] = pw, rPanel_[3] = ph;
    rHelp_[1] = py + ph + 14;
    rHome_ = start;
    remakePrompts(root_, false, goBack);
    bool onNav = false;   // (the keyboard's focus on a seat that went with fewer players: Start)
    for (const Widget* w = root_->child; w && !onNav; w = w->next)
        onNav = w->nav && px_ >= w->x && px_ < w->x + w->w && py_ >= w->y && py_ < w->y + w->h;
    if (pickNew_ || (!onNav && !handShown_)) {   // (the focus / hand on Start: one press of Enter / A, or a click, starts)
        pickNew_ = false;
        px_ = start->x + start->w / 2, py_ = start->y + start->h / 2;
    }
}

// a tab of the strip: 0 "Game" (Options' rows), 1.. the host's categories (onTab)
void Frontend::remakeTab(int i)
{
    walking_ = false;
    if (rTabFn_) {   // remakepages: the page's own tabs (High Scores' tables, the Jukebox's lists)
        const auto f = rTabFn_;
        f(i);
    } else if (i == 0)
        screenOptions();
    else if (onTab) {
        const auto fn = onTab;   // a copy: it rebuilds the page
        fn(i - 1);
    }
}

// a settings page: the tabs, the rows, the help, the prompts
void Frontend::remakeSettings(const std::string& title, std::vector<Extra>* rows, int current, std::function<void()> back)
{
    const int screen = current == 0 ? 2 : 100;
    remakeBegin(screen, "set|" + title);
    rPreviewRow_.clear();
    if (remakeFirstFocus_)
        remakeScroll_ = 0;
    rebuild_ = [this, title, rows, current, back]() { remakeSettings(title, rows, current, back); };
    root_->user = 1;
    // (the panel leaves the prompts' column free at the right where the screen is wide enough)
    const int pw = std::clamp(std::min(int(float(cw_) * 0.78f), cw_ - 216), 520, 900), px = (cw_ - pw) / 2;
    // (blood2, user 2026-10-21 "the text in the settings is a bit too small": the rows' text 15 units (was 11; about
    // 2.4 % of the height for its small capitals), the tabs 14 / 17, the help 11; the tabs and the panel moved up and
    // the panel taller so 12 rows of 26 still fit)
    // the prompts in a row along the bottom (a screen too narrow for their column: 4:3, 3:2): 11 rows, the panel ending
    // above them
    int labelW = 0;
    for (const char* l : {"Right", "Left", "Back", "Accept"})
        labelW = std::max(labelW, int(std::ceil(rTextWidth(l, 12.0f))));
    const bool narrow = cw_ - kPromptMargin - (labelW + int(kPromptGap + kPromptCapW) + 2) < px + pw + 6;
    const int tabY = 38, py = 74, ph = narrow ? 362 : 396;
    rPanel_[0] = px, rPanel_[1] = py, rPanel_[2] = pw, rPanel_[3] = ph;
    // the tabs: "Game" + the categories (a page without the strip: its title on one raised tab)
    std::vector<std::string> names;
    if (current >= 0) {
        names.push_back("Game");
        for (const std::string& t : tabs) names.push_back(t);
        remakeTabs_ = int(names.size());
        rTabCur_ = current;
    } else {
        names.push_back(title);
        remakeTabs_ = 0;
        rTabCur_ = -1;
    }
    const int nt = int(names.size());
    const int tw = current >= 0 ? pw / nt : std::min(pw, 280);
    const int tx0 = current >= 0 ? px : (cw_ - tw) / 2;
    rTabsRtl_ = lang::rtl() && current >= 0;
    for (int i = 0; i < nt; ++i) {
        Widget* t = widgetNew();
        addChild(root_, t);
        const bool cur = current < 0 || i == current;
        setRect(t, tx0 + (rTabsRtl_ ? nt - 1 - i : i) * tw, cur ? tabY - 6 : tabY, tw, cur ? 44 : 38);   // (rtl: from the right)
        t->text = names[size_t(i)];
        t->user = cur ? 1 : 0;
        t->seed = 500 + i * 13;
        t->draw = &Frontend::drawRTab;
        t->flags |= 0x100;
        if (current >= 0) {
            t->help = cur ? names[size_t(i)] + " settings (this page)" : "Open the " + names[size_t(i)] + " settings";
            if (!cur)
                setHandlers(t, [this, i](Widget&) { remakeTab(i); }, &Frontend::hitRect);
        }
    }
    // the rows (scrolled: the focus past the last one shown moves them)
    // relfix2 (user 2026-11-03: the pictures covered the help line and the rows): a page with comparison pictures keeps
    // a strip of kPrvRows rows' height under its rows for them (above the help line); fewer rows shown, nothing covered
    bool anyPreview = false;
    for (const Extra& e : *rows)
        anyPreview = anyPreview || (e.preview && e.choices.size() >= 2);
    constexpr int kPrvRows = 4;
    const int rowTop = py + 12, rowH = 26, fullRows = narrow ? 11 : 12, maxRows = fullRows - (anyPreview ? kPrvRows : 0);
    const int n = int(rows->size());
    remakeScroll_ = std::clamp(remakeScroll_, 0, std::max(0, n - maxRows));
    const int shown = std::min(n - remakeScroll_, maxRows);
    if (ddRow_ >= 0 && (ddRow_ < remakeScroll_ || ddRow_ >= remakeScroll_ + shown))
        ddRow_ = -1;   // (remakefix: its row gone)
    const bool dd = ddOpen();
    for (int r = 0; r < shown; ++r) {
        const int i = remakeScroll_ + r;
        const Extra& e = (*rows)[size_t(i)];
        Widget* w = widgetNew();
        addChild(root_, w);
        setRect(w, px + 22, rowTop + r * rowH, pw - 44, rowH - 2);
        const std::string off = e.off ? e.off() : std::string();
        const bool on = e.click && off.empty();
        const bool exp = e.experimental && e.experimental();
        w->text = e.name;
        w->raw = e.raw;
        w->value = e.value ? e.value() : std::string();
        w->user = on ? 0 : 1;
        w->seed = exp ? 1 : 0;
        w->draw = &Frontend::drawRRow;
        w->flags |= 0x100;
        w->help = !off.empty() ? off : (exp && e.help.rfind("Experimental", 0) != 0 ? "Experimental: " + e.help : e.help);
        if (e.preview && on && !dd && e.choices.size() >= 2) {   // fix28: the comparison pictures, shown with the focus
            // relfix2: in the strip under the rows (below the "more" arrow, above the help line), its whole width; the
            // pictures sized and centred in it by drawRPreview (relfix's beside-the-labels placement: the rows now free)
            const int by = rowTop + maxRows * rowH + 14, bh = kPrvRows * rowH - 16;
            Widget* pv = widgetNew();
            addChild(root_, pv);
            setRect(pv, px + 22, by, pw - 44 - 10, bh);   // (clear of the scroll bar at the right)
            pv->user = i;
            pv->draw = &Frontend::drawRPreview;
            pv->flags |= 0x100;
            pv->over = true;
            rPreviewRow_[pv] = w;
            rPreviewRows_ = rows;
        }
        if (e.swatch && on) {   // menufb: the colour square left of the value
            const int vw = int(std::ceil(rTextWidth(w->value, 15.0f)));
            int sx = px + pw - 22 - 6 - 18 - vw - 12 - 30;
            if (lang::rtl())
                sx = 2 * (px + 22) + (pw - 44) - sx - 30;
            Widget* sw = widgetNew();
            addChild(root_, sw);
            setRect(sw, sx, rowTop + r * rowH + 3, 30, rowH - 8);
            sw->tint = e.swatch();
            sw->value = std::to_string(sw->tint);
            sw->draw = &Frontend::drawSwatch;
            sw->flags |= 0x100;
            sw->over = !dd;
        }
        // menufb: a value bar (the volumes, the blood colour's red / green / blue) left of the value
        if (e.barGet && on && !dd) {
            const int vw = int(std::ceil(std::max(rTextWidth(w->value, 15.0f), rTextWidth("100%", 15.0f)))), bw2 = std::min(170, pw / 4);
            int bx = px + pw - 22 - 6 - 18 - vw - 16 - bw2;
            if (lang::rtl())
                bx = 2 * (px + 22) + (pw - 44) - bx - bw2;
            sliderBar(root_, bx, rowTop + r * rowH + 3, bw2, rowH - 8, e.barGet(), e.barMax, e.barStep, [this, rows, i](int v) {
                if ((*rows)[size_t(i)].barSet) {
                    const auto set = (*rows)[size_t(i)].barSet;
                    set(v);
                }
                walking_ = false;
                if (rebuild_) {
                    const auto f = rebuild_;
                    f();
                }
            });
        }
        // remakefix: the dropdown open: the rows wait (no hover, no click: a click outside the list closes it)
        if (!on || dd)
            continue;
        const std::string key = remakeKey_;
        auto after = [this, screen, key]() {   // the new value: the page again (another page opened: left alone)
            walking_ = false;
            if (screen_ == screen && remakeKey_ == key && rebuild_) {
                const auto f = rebuild_;
                f();
            }
        };
        auto step = [this, rows, i, after]() {
            if ((*rows)[size_t(i)].click) {   // a copy: the click may replace the rows
                const auto click = (*rows)[size_t(i)].click;
                click();
            }
            after();
        };
        // remakefix (user 2026-10-22): a choice opens its dropdown (optui, user 2026-10-26: every row with a fixed list,
        // any length: a long one scrolls); Right steps as the click did
        const bool list = e.choices.size() >= 2 && e.value;
        if (list) {
            setHandlers(w, [this, i, after](Widget&) {
                ddRow_ = i;
                ddKey_ = remakeKey_;
                ddFocus_ = true;
                ddScroll_ = -1;   // (optui: scrolled to show the current value)
                after();
            }, &Frontend::hitRect);
            w->right = step;
        } else
            setHandlers(w, [step](Widget&) { step(); }, &Frontend::hitRect);
        w->left = [this, rows, i, after]() {
            const auto fn = (*rows)[size_t(i)].clickBack ? (*rows)[size_t(i)].clickBack : (*rows)[size_t(i)].click;
            if (fn) fn();
            after();
        };
        // padpass: a value bar (the volumes, the custom blood's red / green / blue): Left / Right (the d-pad, the stick,
        // held: repeated) move it down / up a step and stop at its ends (before: Left stepped up like a click and both
        // wrapped round to 0)
        if (e.barGet && e.barSet) {
            const int stepK = e.barMax >= 200 ? 16 : std::max(1, e.barStep);   // (prerel: the volumes' 5 %)
            auto barStep = [this, rows, i, after, stepK](int dir) {
                const Extra& x = (*rows)[size_t(i)];
                if (!x.barGet || !x.barSet)
                    return;
                const int v = x.barGet(), n = std::clamp(dir > 0 ? (v / stepK + 1) * stepK : (v + stepK - 1) / stepK * stepK - stepK, 0, x.barMax);
                if (n != v) {
                    const auto set = x.barSet;
                    set(n);
                }
                after();
            };
            w->left = [barStep]() { barStep(-1); };
            w->right = [barStep]() { barStep(1); };
        }
        w->nav = true;
    }
    if (dd)
        rScroll_ = nullptr;   // (the list's own Up / Down)
    else
        rScroll_ = [this, n, maxRows](int dir) {
            const int s = std::clamp(remakeScroll_ + dir, 0, std::max(0, n - maxRows));
            if (s == remakeScroll_)
                return false;
            remakeScroll_ = s;
            const auto f = rebuild_;
            f();
            return true;
        };
    for (int d = 0; d < 2 && !dd; ++d) {   // the scroll arrows (more rows above / below)
        if (d == 0 ? remakeScroll_ == 0 : remakeScroll_ + shown >= n)
            continue;
        Widget* a = widgetNew();
        addChild(root_, a);
        // (Light Gun Edition: the "more below" arrow a wide, tall target for a gun: under the rows, most of their width)
        if (d) setRect(a, px + 40, rowTop + maxRows * rowH, pw - 100, 26);
        else setRect(a, px + pw / 2 - 20, rowTop - 12, 40, 13);
        a->user = d;
        a->draw = &Frontend::drawRArrow;
        a->flags |= 0x100;
        a->help = d ? "More settings below" : "More settings above";
        setHandlers(a, [this, d, maxRows](Widget&) {
            walking_ = false;
            if (rScroll_) {
                const auto f = rScroll_;
                f(d ? maxRows - 1 : 1 - maxRows);
            }
        }, &Frontend::hitRect);
    }
    // Light Gun Edition (user 2026-11-05: "easier to navigate up and down with a gun"): beside the rows at the right, two
    // tall targets over the scroll bar: the upper half pages up, the lower half pages down (a shot anywhere in them)
    if (n > maxRows && !dd)
        for (int d = 0; d < 2; ++d) {
            const bool can = d == 0 ? remakeScroll_ > 0 : remakeScroll_ + shown < n;
            Widget* a = widgetNew();
            addChild(root_, a);
            const int half = maxRows * rowH / 2;
            setRect(a, px + pw - 30, rowTop + d * half, 44, half - 2);
            a->user = d;
            a->draw = can ? &Frontend::drawRArrow : nullptr;
            a->flags |= 0x100;
            a->help = d ? "Shoot here: the next page of settings" : "Shoot here: the page of settings above";
            if (can)
                setHandlers(a, [this, d, maxRows](Widget&) {
                    walking_ = false;
                    if (rScroll_) {
                        const auto f = rScroll_;
                        f(d ? maxRows - 1 : 1 - maxRows);
                    }
                }, &Frontend::hitRect);
        }
    // menufb (user 2026-10-26: "no indication that the list is scrollable"): a scroll bar at the rows' right
    if (n > maxRows && !dd)
        scrollBar(root_, px + pw - 19, rowTop, 7, maxRows * rowH - 2, remakeScroll_, maxRows, n, [this](int first) {
            remakeScroll_ = first;
            ddRow_ = -1;
            if (rebuild_) {
                const auto f = rebuild_;
                f();
            }
        });
    // the rows' sight pictures (the dropdowns' icons; menufb: the floating preview became the Firing Range's page)
    artRows_ = rows;
    artSig_.clear();
    for (int i = 0; i < n; ++i)
        if ((*rows)[size_t(i)].art)
            artSig_ += (*rows)[size_t(i)].value ? (*rows)[size_t(i)].value() + "|" : std::string("|");
    if (extrasRange() && rangeSig)
        artSig_ += "#" + rangeSig();
    // menufb: the Firing Range under the rows (its page: the rows fit, no scrolling)
    if (extrasRange() && n <= maxRows) {
        const int top = rowTop + n * rowH + 4, bottom = rowTop + maxRows * rowH - 4;
        if (bottom - top >= 64)   // (padpass: 70 on a 4:3 screen's 11 rows: the range was missing there)
            rangeWidgets(root_, px + 22, top, pw - 44, bottom - top);
    }
    rHelp_[0] = px + (pw - 110) / 2, rHelp_[1] = rowTop + fullRows * rowH + 17, rHelp_[2] = pw - 150;
    remakePrompts(root_, current >= 0, dd ? std::function<void()>([this]() { ddClose(); }) : back);
    // remakefix: the dropdown (over everything): under its row's value (above it when there is no room below), the
    // values on a small dark torn panel, the current one marked; picking one steps the row's click to it
    if (dd) {
        const int r = ddRow_ - remakeScroll_;
        const Extra& e = (*rows)[size_t(ddRow_)];
        const std::vector<std::string> ch = e.choices;
        const std::string cur = e.value ? e.value() : std::string();
        const int curIdx = e.choiceNow ? e.choiceNow() : -1;
        // optui: a crosshair row's values drawn as their icons (taller items); a long list shows up to 10 (7 with icons)
        // values and scrolls (Up / Down past its ends, the wheel, its arrows), its first one ddScroll_
        const bool icons = bool(e.art);
        const int itemH = icons ? 28 : 20, m = 5, nItems = int(ch.size());
        float tw = 0;
        for (const std::string& c : ch)
            tw = std::max(tw, rTextWidth(c, 13.0f));
        const int rx = px + 22, ry = rowTop + r * rowH, rw = pw - 44;
        const int lw0 = std::max(150, int(std::ceil(tw)) + 46 + (icons ? 30 : 0));
        const int below = kHeight - 6 - (ry + rowH), above = ry - 2 - 6;
        int vis = std::min(nItems, icons ? 7 : 10);
        auto heightOf = [&](int v) { return v * itemH + 2 * m + (v < nItems ? 24 : 0); };   // (the arrows' room)
        while (vis > 3 && heightOf(vis) > std::max(below, above))
            --vis;
        const bool scroll = vis < nItems;
        const int lh = heightOf(vis), arrowH = scroll ? 12 : 0, sbW = scroll ? 14 : 0;   // (menufb: its scroll bar's room)
        const int lw = lw0 + sbW;
        const int lx = rx + rw - lw;
        int ly = ry + rowH;
        if (ly + lh > kHeight - 6)
            ly = std::max(6, ry - lh - 2);
        int curK = -1;
        for (int k = 0; k < nItems; ++k)
            if (curIdx >= 0 ? k == curIdx : ch[size_t(k)] == cur)
                curK = k;
        if (ddScroll_ < 0)   // (just opened: the current value in view, near the middle)
            ddScroll_ = curK >= 0 ? curK - vis / 2 : 0;
        ddScroll_ = std::clamp(ddScroll_, 0, nItems - vis);
        ddRows_ = rows;
        ddRect_[0] = lx, ddRect_[1] = ly, ddRect_[2] = lw, ddRect_[3] = lh;
        int added = 0;
        Widget* panel = widgetNew();
        addChild(root_, panel);
        ++added;
        setRect(panel, lx, ly, lw, lh);
        panel->draw = &Frontend::drawRDrop;
        panel->flags |= 0x100;
        panel->seed = 700 + ddRow_ * 7;
        Widget* focus = nullptr;
        for (int v = 0; v < vis; ++v) {
            const int k = ddScroll_ + v;
            Widget* it = widgetNew();
            addChild(root_, it);
            ++added;
            setRect(it, lx + 4, ly + m + arrowH + v * itemH, lw - 8 - sbW, itemH);
            it->text = ch[size_t(k)];
            it->id = int16_t(k);
            it->value = icons ? artSig_ : std::string();   // (the icons: drawn again when the settings change)
            it->user = (k == curK ? 1 : 0) | (e.choiceOff && e.choiceOff(k) ? 2 : 0);   // (padfix: 2 = not available: grey)
            it->seed = 720 + k;
            it->help = e.help;
            it->draw = &Frontend::drawRDropItem;
            it->flags |= 0x100;
            it->nav = true;
            if (it->user & 1)
                focus = it;
            setHandlers(it, [this, rows, i = ddRow_, k, screen](Widget&) {
                walking_ = false;
                pickChoice(*rows, i, k);   // (set at once, else the row's own steps: its save and hooks as a click)
                if (screen_ == screen && sc_ > 1)
                    ddClose();   // (the page again, the focus back on the row)
                else
                    ddRow_ = -1;
            }, &Frontend::hitRect);
        }
        // the list scrolled by dir values (Up / Down past its ends, the wheel, the arrows)
        rScroll_ = [this, nItems, vis](int dir) {
            const int s = std::clamp(ddScroll_ + dir, 0, nItems - vis);
            if (s == ddScroll_)
                return false;
            ddScroll_ = s;
            const auto f = rebuild_;
            f();
            return true;
        };
        for (int d = 0; d < 2 && scroll; ++d) {   // (more values above / below)
            if (d == 0 ? ddScroll_ == 0 : ddScroll_ + vis >= nItems)
                continue;
            Widget* a = widgetNew();
            addChild(root_, a);
            ++added;
            setRect(a, lx + (lw - sbW) / 2 - 20, d ? ly + lh - m - arrowH : ly + m, 40, arrowH);
            a->user = d;
            a->draw = &Frontend::drawRArrow;
            a->flags |= 0x100;
            a->help = e.help;
            setHandlers(a, [this, d, vis](Widget&) {
                walking_ = false;
                if (rScroll_) {
                    const auto f = rScroll_;
                    f(d ? vis - 1 : 1 - vis);
                }
            }, &Frontend::hitRect);
        }
        if (scroll) {   // menufb: the list's scroll bar at its right
            scrollBar(root_, lx + lw - sbW - 2, ly + m + arrowH, 7, vis * itemH, ddScroll_, vis, nItems, [this](int first) {
                ddScroll_ = first;
                if (rebuild_) {
                    const auto f = rebuild_;
                    f();
                }
            });
            ++added;
        }
        {   // drawn last (over the rows): addChild puts the newest first, the walks draw in the list's order; the
            // panel and its values (added, newest first at the head) moved to the list's end, the panel first
            std::vector<Widget*> mine;
            for (int k = 0; k < added && root_->child; ++k) {
                Widget* h = root_->child;
                root_->child = h->next;
                h->next = nullptr;
                mine.insert(mine.begin(), h);
            }
            Widget** tail = &root_->child;
            while (*tail)
                tail = &(*tail)->next;
            for (Widget* m2 : mine) {
                *tail = m2;
                tail = &m2->next;
            }
        }
        if (ddFocus_ && !handShown_ && focus)   // (opened by a key: the focus on the current value)
            px_ = focus->x + focus->w / 2, py_ = focus->y + focus->h / 2;
        ddFocus_ = false;
        return;
    }
    if (remakeFirstFocus_ && !handShown_)
        remakeFocus(0);
}

// remakefix: the dropdown closed (Escape / B, Back, a click outside): the page again, the focus back on its row
void Frontend::ddClose()
{
    if (ddRow_ < 0)
        return;
    int row = ddRow_ - remakeScroll_;
    ddRow_ = -1;
    walking_ = false;
    if (rebuild_ && sc_ > 1) {
        const auto f = rebuild_;
        f();
    }
    if (!handShown_) {   // (the focus on the row it came from; the children are newest first: by their y)
        std::vector<Widget*> rs;
        for (Widget* w = root_ ? root_->child : nullptr; w; w = w->next)
            if (w->draw == &Frontend::drawRRow)
                rs.push_back(w);
        std::sort(rs.begin(), rs.end(), [](const Widget* a, const Widget* b) { return a->y < b->y; });
        if (row >= 0 && row < int(rs.size()))
            px_ = rs[size_t(row)]->x + rs[size_t(row)]->w / 2, py_ = rs[size_t(row)]->y + rs[size_t(row)]->h / 2;
    }
}

// the controls prompts at the bottom right: Accept (Enter / A), Back (Esc / B), on a page with tabs Left (Q / LB) and
// Right (E / RB); the mouse can click Back, Left and Right
void Frontend::remakePrompts(Widget* root, bool tabsKeys, std::function<void()> back)
{
    struct P { const char* label; const char* key; const char* pad; std::function<void()> act; int tag; };
    std::vector<P> ps;
    if (tabsKeys && !gunPrompts) {
        auto step = [this](int d) {
            return [this, d]() {
                if (remakeTabs_ > 0)
                    remakeTab((rTabCur_ + (rTabsRtl_ ? -d : d) + remakeTabs_) % remakeTabs_);
            };
        };
        ps.push_back({"Right", "E", "RB", step(1), 0});
        ps.push_back({"Left", "Q", "LB", step(-1), 0});
    }
    const bool top = screen_ == 1 && !debugItems_ && mainGroup_ == 0;
    // (the main menu: Escape is its last item's, Quit / Back; the pages: this prompt is Escape's widget)
    if (gunPrompts) {   // Light Gun Edition: the gun's buttons (reload = Back / Quit, the trigger = Accept)
        ps.push_back({top ? "Quit" : "Back", "Reload", "Reload", back, screen_ == 1 || screen_ == 21 ? 0 : 1});
        ps.push_back({"Accept", "Shoot", "Shoot", nullptr, 0});
    } else {
        ps.push_back({top ? "Quit" : "Back", "Esc", "B", back, screen_ == 1 || screen_ == 21 ? 0 : 1});
        ps.push_back({"Accept", "Enter", "A", nullptr, 0});
    }
    const int n = int(ps.size());
    // a tidy column at the bottom right (the margin kPromptMargin, kPromptStep a row; the labels right-aligned a gap
    // left of the key caps); a column that would run into a settings panel (narrow screens): one row along the bottom
    int labelW = 0;
    for (const P& p : ps)
        labelW = std::max(labelW, int(std::ceil(rTextWidth(p.label, 12.0f))));
    const int colW = labelW + int(kPromptGap + kPromptCapW) + 2;
    const int colX = cw_ - kPromptMargin - colW, rowY = kHeight - kPromptMargin - 24;
    // (leftovers, rtl: mirrored: the column at the bottom left, the caps at its left, the labels after them)
    const bool rtl = lang::rtl();
    const bool row = screen_ != 1 && screen_ != 21 &&
                     (rtl ? cw_ - colX > rPanel_[0] - 6 : colX < rPanel_[0] + rPanel_[2] + 6) &&
                     rowY - (n - 1) * kPromptStep < rPanel_[1] + rPanel_[3];
    int rx = cw_ - kPromptMargin;   // (the row: from the right)
    for (int k = n - 1; k >= 0; --k) {
        Widget* w = widgetNew();
        addChild(root, w);
        if (row) {
            const int iw = int(std::ceil(rTextWidth(ps[size_t(k)].label, 12.0f))) + int(kPromptGap + kPromptCapW) + 2;
            setRect(w, rx - iw, rowY, iw, 24);
            rx -= iw + 18;
        } else
            setRect(w, colX, rowY - (n - 1 - k) * kPromptStep, colW, 24);
        if (rtl) {
            w->x = cw_ - w->x - w->w;
            w->rtlRow = true;
        }
        w->text = ps[size_t(k)].label;
        w->value = std::string(ps[size_t(k)].key) + "|" + ps[size_t(k)].pad;
        w->seed = 900 + k * 5;
        w->draw = &Frontend::drawRPrompt;
        w->flags |= 0x100;
        w->tag = ps[size_t(k)].tag;
        if (ps[size_t(k)].act) {
            setHandlers(w, [this, a = ps[size_t(k)].act](Widget&) {
                walking_ = false;
                a();
            }, &Frontend::hitRect);
        }
    }
}

// ---- the keys: Up / Down the focus, Left / Right a row's value, Q / E (the shoulder buttons) the tabs ----
void Frontend::remakeFocus(int dir)
{
    std::vector<Widget*> nav;
    for (Widget* w = root_ ? root_->child : nullptr; w; w = w->next)
        if (w->nav && w->activate)
            nav.push_back(w);
    if (nav.empty())
        return;
    std::sort(nav.begin(), nav.end(), [](const Widget* a, const Widget* b) { return a->y != b->y ? a->y < b->y : a->x < b->x; });
    int cur = -1;
    for (size_t i = 0; i < nav.size(); ++i)
        if (px_ >= nav[i]->x && px_ < nav[i]->x + nav[i]->w && py_ >= nav[i]->y && py_ < nav[i]->y + nav[i]->h)
            cur = int(i);
    int next;
    if (cur < 0)
        next = dir < 0 ? int(nav.size()) - 1 : 0;
    else if (dir == 0)
        next = cur;
    else {
        next = cur + dir;
        if (next < 0 || next >= int(nav.size())) {
            if (rScroll_) {   // a settings page: the rows move (the focus stays at the same place: the next row)
                // ipolish: past grey rows (no focus) the rows go on moving until one that takes the focus is under it
                // (before: the focus fell onto the grey row and the next press went back to the top)
                bool moved = false;
                for (int k = 0; k < 64 && rScroll_; ++k) {
                    const auto f = rScroll_;
                    if (!f(dir))
                        break;
                    moved = true;
                    if (!focusText().empty())
                        break;
                }
                if (moved) {
                    remakeCue();   // (remakefix: a move's sound)
                    if (focusText().empty()) {   // (the end reached on grey rows: the last row that takes it that way)
                        Widget* best = nullptr;
                        for (Widget* w = root_ ? root_->child : nullptr; w; w = w->next)
                            if (w->nav && w->activate && (!best || (dir > 0 ? w->y > best->y : w->y < best->y)))
                                best = w;
                        if (best)
                            px_ = best->x + best->w / 2, py_ = best->y + best->h / 2;
                    }
                    return;
                }
                // relmenu (user 2026-10-30: at the very bottom, Down went to the first row shown, not the page's top):
                // past the end the list goes round: scrolled to its other end, the focus on its first / last row
                const auto f = rScroll_;
                if (f(-dir * 100000)) {
                    std::vector<Widget*> nv;
                    for (Widget* w = root_ ? root_->child : nullptr; w; w = w->next)
                        if (w->nav && w->activate)
                            nv.push_back(w);
                    if (!nv.empty()) {
                        std::sort(nv.begin(), nv.end(), [](const Widget* a, const Widget* b) { return a->y != b->y ? a->y < b->y : a->x < b->x; });
                        const Widget* t = dir > 0 ? nv.front() : nv.back();
                        px_ = t->x + t->w / 2, py_ = t->y + t->h / 2;
                    }
                    remakeCue();
                    return;
                }
            }
            next = (next + int(nav.size())) % int(nav.size());
        }
    }
    if (dir != 0 && next != cur)
        remakeCue();   // (remakefix: a move's sound)
    px_ = nav[size_t(next)]->x + nav[size_t(next)]->w / 2;
    py_ = nav[size_t(next)]->y + nav[size_t(next)]->h / 2;
}

void Frontend::remakeNav(FrontendInput& in, uint32_t nowMs)
{
    // remakefix: the dropdown open: a click outside its list closes it (and does nothing else)
    if (ddOpen() && in.click && root_) {
        const int x = in.mouseMoved ? in.mouseX : px_, y = in.mouseMoved ? in.mouseY : py_;
        if (x < ddRect_[0] || x >= ddRect_[0] + ddRect_[2] || y < ddRect_[1] || y >= ddRect_[1] + ddRect_[3]) {
            in.press = in.click = false;
            remakeCue();
            ddClose();
        }
    }
    // optui: the wheel scrolls a settings page's rows, or the open dropdown's values
    for (int k = 0; k < std::min(std::abs(in.wheel), 8) && rScroll_; ++k) {
        const auto f = rScroll_;
        if (!f(in.wheel > 0 ? -1 : 1))
            break;
    }
    // blood2: a new splatter: R3 / the B key, or a click on the backdrop (outside the panel and every widget)
    if (galleryRE()) {   // (regallery: the Gallery's pages show no splatter; a click on the stage turns the model)
    } else if (in.newSplat)
        newSplatter();
    else if (in.click && root_) {
        const int x = in.mouseMoved ? in.mouseX : px_, y = in.mouseMoved ? in.mouseY : py_;
        bool hit = x >= rPanel_[0] && x < rPanel_[0] + rPanel_[2] && y >= rPanel_[1] && y < rPanel_[1] + rPanel_[3];
        for (const Widget* w = root_->child; w && !hit; w = w->next)
            hit = x >= w->x && x < w->x + w->w && y >= w->y && y < w->y + w->h;
        if (!hit)
            newSplatter();
    }
    const bool held[4] = {in.up, in.down, in.left, in.right};
    bool edge[4] = {};
    for (int k = 0; k < 4; ++k) {
        if (held[k] && !navHeld_[k]) {
            edge[k] = true;
            navNext_[k] = nowMs + 380;   // (held: again after 380 ms, then every 110 ms)
        } else if (held[k] && int(nowMs - navNext_[k]) >= 0) {
            edge[k] = true;
            navNext_[k] = nowMs + 110;
        }
        navHeld_[k] = held[k];
    }
    const bool any = edge[0] || edge[1] || edge[2] || edge[3] || in.tabPrev || in.tabNext;
    // the keyboard / a pad took over: the focus, no hand (remakefix, user 2026-10-22 "sometimes the mouse disappears
    // with clicks": a mouse click is a press too and the host counted its button as the keyboard: only a key's / pad
    // button's press hides the hand, never the mouse's click)
    if (handShown_ && (any || (in.press && !in.click)) && (any || in.keyboard || in.pad))
        handShown_ = false;
    if (!any)
        return;
    idleSince_ = nowMs;
    if (ddOpen()) {   // remakefix: the list's Up / Down only (Left / Right / the tabs wait until it closes)
        if (edge[0] || edge[1])
            remakeFocus(edge[0] ? -1 : 1);
        return;
    }
    if (in.tabPrev || in.tabNext) {
        if (remakeTabs_ > 0 && rTabCur_ >= 0) {
            remakeCue();   // (remakefix, user 2026-10-22 "Q and E do not make a sound")
            remakeTab((rTabCur_ + (in.tabNext != rTabsRtl_ ? 1 : -1) + remakeTabs_) % remakeTabs_);
            return;
        }
    }
    if (rGrid_ && (edge[0] || edge[1] || edge[2] || edge[3])) {   // remakepages: cards / PC Mode: the nearest that way
        remakeFocusDir(edge[2] ? -1 : edge[3] ? 1 : 0, edge[0] ? -1 : edge[1] ? 1 : 0);
        return;
    }
    if (edge[0] || edge[1]) {
        remakeFocus(edge[0] ? -1 : 1);
        return;
    }
    if (edge[2] || edge[3]) {   // a settings row under the focus: its value before / next
        for (Widget* w = root_ ? root_->child : nullptr; w; w = w->next) {
            // (actorswap: the Cut-scenes page's Actors rows, drawReOpt with Left / Right of their own, too)
            if ((w->draw != &Frontend::drawRRow && !(w->draw == &Frontend::drawReOpt && w->left)) || !w->activate)
                continue;
            if (px_ < w->x || px_ >= w->x + w->w || py_ < w->y || py_ >= w->y + w->h)
                continue;
            remakeCue();   // (the click's sample)
            walking_ = false;
            if (edge[2] && w->left) {
                const auto f = w->left;
                f();
            } else if (edge[3] && w->right) {   // (remakefix: a row with a dropdown: Right steps, Enter opens it)
                const auto f = w->right;
                f();
            } else {
                const auto f = w->activate;
                f(*w);
            }
            return;
        }
        if (remakeTabs_ > 0 && rTabCur_ >= 0 && screen_ != 1) {   // (no row: the tabs)
            remakeCue();
            remakeTab((rTabCur_ + (edge[3] != rTabsRtl_ ? 1 : -1) + remakeTabs_) % remakeTabs_);
            return;
        }
    }
}

// ---- drawing ----
const std::vector<uint8_t>& Frontend::glyphImage(int g) const
{
    auto it = glyphCache_.find(g);
    if (it == glyphCache_.end())
        it = glyphCache_.emplace(g, spr_.image(g)).first;
    return it->second;
}

void Frontend::rBlend(int x, int y, uint32_t rgb, float a)
{
    x -= rOffX_, y -= rOffY_;
    if (x < 0 || y < 0 || x >= rW_ || y >= rH_ || a <= 0.0f)
        return;
    a = std::min(a, 1.0f);
    uint8_t* d = &(*canvas_)[(size_t(y) * size_t(rW_) + size_t(x)) * 4];
    const float da = d[3] / 255.0f, oa = a + da * (1 - a);
    if (oa <= 0)
        return;
    const float src[3] = {float((rgb >> 16) & 255), float((rgb >> 8) & 255), float(rgb & 255)};
    for (int c = 0; c < 3; ++c)
        d[c] = uint8_t(std::clamp((src[c] * a + d[c] * da * (1 - a)) / oa + 0.5f, 0.0f, 255.0f));
    d[3] = uint8_t(oa * 255.0f + 0.5f);
}

// a piece drawn once into a tile (the key: what it is and where), then laid over the target each time
void Frontend::rCached(const std::string& key, float x, float y, float w, float h, float margin, const std::function<void()>& draw)
{
    if (tilesW_ != rpw() * 4096 + rph() || tilesHue_ != bloodHue_ || tilesSeed_ != splatSeed || tiles_.size() > 160) {
        tilesHue_ = bloodHue_;
        tilesSeed_ = splatSeed;
        tiles_.clear();
        tilesW_ = rpw() * 4096 + rph();
    }
    auto it = tiles_.find(key);
    if (it == tiles_.end()) {
        const float S = ps_;
        Tile t;
        t.x0 = std::max(0, int((x - margin) * S));
        t.y0 = std::max(0, int((y - margin) * S));
        t.w = std::max(0, std::min(int((x + w + margin) * S) + 1, rpw()) - t.x0);
        t.h = std::max(0, std::min(int((y + h + margin) * S) + 1, rph()) - t.y0);
        t.rgba.assign(size_t(t.w) * size_t(t.h) * 4, 0);
        std::vector<uint8_t>* saveC = canvas_;
        const int sx = rOffX_, sy = rOffY_, sw = rW_, sh = rH_;
        canvas_ = &t.rgba, rOffX_ = t.x0, rOffY_ = t.y0, rW_ = t.w, rH_ = t.h;
        ++tileDepth_;
        draw();
        --tileDepth_;
        canvas_ = saveC, rOffX_ = sx, rOffY_ = sy, rW_ = sw, rH_ = sh;
        it = tiles_.emplace(key, std::move(t)).first;
    }
    const Tile& t = it->second;
    // menuperf: an opaque pixel, or one over an empty one, is copied (what rBlend gives there: the same bytes); the
    // others blended (a page's static layer starts empty: its big torn panel was ~15 ms of blending at 1800 lines)
    for (int j = 0; j < t.h; ++j) {
        const int ty = t.y0 + j - rOffY_;
        if (ty < 0 || ty >= rH_)
            continue;
        for (int i = 0; i < t.w; ++i) {
            const uint8_t* p = &t.rgba[(size_t(j) * size_t(t.w) + size_t(i)) * 4];
            if (!p[3])
                continue;
            const int tx = t.x0 + i - rOffX_;
            if (tx < 0 || tx >= rW_)
                continue;
            uint8_t* d = &(*canvas_)[(size_t(ty) * size_t(rW_) + size_t(tx)) * 4];
            if (p[3] == 255 || d[3] == 0)
                std::memcpy(d, p, 4);
            else
                rBlend(t.x0 + i, t.y0 + j, (uint32_t(p[0]) << 16) | (uint32_t(p[1]) << 8) | p[2], p[3] / 255.0f);
        }
    }
}

// the text: grungy small capitals from the PC's white 32 font (the first letter of each word taller), coloured,
// with worn holes (distress 0..1)
// the letters drawn tall: a word's first, the digits, and every letter of a word written in capitals (PC, CRT)
static std::vector<bool> tallLetters(const std::string& s)
{
    std::vector<bool> tall(s.size(), false);
    for (size_t i = 0; i < s.size();) {
        if (s[i] == ' ') { ++i; continue; }
        size_t e = i;
        bool caps = true;
        int letters = 0;
        while (e < s.size() && s[e] != ' ') {
            if (s[e] >= 'a' && s[e] <= 'z') caps = false;
            if ((s[e] >= 'a' && s[e] <= 'z') || (s[e] >= 'A' && s[e] <= 'Z') || (s[e] >= '0' && s[e] <= '9')) ++letters;
            ++e;
        }
        for (size_t k = i; k < e; ++k)
            tall[k] = k == i || (caps && letters > 1) || (s[k] >= '0' && s[k] <= '9');
        i = e;
    }
    return tall;
}

float Frontend::rTextWidth(const std::string& s, float h) const
{
    return rTextWidthRaw(langOn() ? lang::tr(s) : s, h);
}

// (lang: of a text already translated; the language's font, else the PC font's bytes)
float Frontend::rTextWidthRaw(const std::string& sIn, float h) const
{
    if (const render::TtfText* tf = lang::fontFor(sIn))
        return tf->width(lang::upperCase() ? upper(sIn) : sIn, lang::pxForCap(h, tf));
    const std::string& s = lang::plain(sIn) ? sIn : pcBytes(sIn);
    const int capH = std::max(1, spr_.height(glyph('A', 0x10000)));
    float w = 0;
    const std::vector<bool> tall = tallLetters(s);
    const std::string u = upperPc(s);
    for (size_t i = 0; i < u.size(); ++i) {
        const char ch = u[i];
        if (ch == ' ') {
            w += h * 0.42f;
            continue;
        }
        const float f = (tall[i] ? h : h * 0.78f) / float(capH);
        const int g = glyph(uint8_t(ch), 0x10000);
        w += g ? float(spr_.width(g) + 1) * f : h * 0.5f;
    }
    return w;
}

void Frontend::rText(const std::string& sIn, float x, float y, float h, uint32_t rgb, int align, float distress, int seed, int fx)
{
    const int capH = std::max(1, spr_.height(glyph('A', 0x10000)));
    // lang: the translation (English: the text as given); the language's font, else the PC font's bytes
    const std::string sTr = langOn() ? lang::tr(sIn) : std::string();
    const std::string& sU = langOn() ? sTr : sIn;
    const render::TtfText* tf = lang::fontFor(sU);
    const bool latin1 = !tf && !lang::plain(sU);
    const std::string sB = latin1 ? pcBytes(sU) : std::string();
    const std::string& s = latin1 ? sB : sU;
    const float tw = rTextWidthRaw(sU, h);
    if (align == 1) x -= tw / 2;
    else if (align == 2) x -= tw;
    if (langCheck)
        langOverflow(sU, int(x), int(tw), 0, nullptr);
    const float S = ps_;
    // optui (menu perf: a list scrolling / a dropdown opening drew every text again, most of a 17-29 ms page): the
    // text starts on a whole pixel, its picture (shadow, outline, letters) is kept by what it is and laid down again
    // wherever the same text goes (a scrolled row, the page built again)
    x = std::round(x * S) / S, y = std::round(y * S) / S;
    const int ox = int(std::lround(x * S)), oy = int(std::lround(y * S));
    char kb[96];
    std::snprintf(kb, sizeof kb, "|%g|%06X|%g|%d|%d|%g", h, rgb & 0xFFFFFF, distress, seed, fx, S);
    const std::string tkey = s + kb;
    auto blitText = [&](const TextTile& t) {
        for (int j = 0; j < t.h; ++j) {
            const int ty = oy + t.dy + j - rOffY_;
            if (ty < 0 || ty >= rH_)
                continue;
            for (int i = 0; i < t.w; ++i) {
                const uint8_t* p = &t.rgba[(size_t(j) * size_t(t.w) + size_t(i)) * 4];
                if (!p[3])
                    continue;
                const int tx = ox + t.dx + i - rOffX_;
                if (tx < 0 || tx >= rW_)
                    continue;
                uint8_t* d = &(*canvas_)[(size_t(ty) * size_t(rW_) + size_t(tx)) * 4];
                if (p[3] == 255 || d[3] == 0)
                    std::memcpy(d, p, 4);
                else
                    rBlend(ox + t.dx + i, oy + t.dy + j, (uint32_t(p[0]) << 16) | (uint32_t(p[1]) << 8) | p[2], p[3] / 255.0f);
            }
        }
    };
    if (const auto it = textCache_.find(tkey); it != textCache_.end()) {
        blitText(it->second);
        return;
    }
    // the letters go into a layer first (premultiplied colour + coverage), then onto the target: its drop shadow, its
    // outline (the coverage grown by r pixels), the letters
    const int m = 8;
    const float xs0 = x;   // (optui: the glyphs placed from the whole pixel (ox, oy): the same picture anywhere)
    const int LX = ox - m, LY = int(std::floor(float(oy) - h * 0.35f * S)) - m;
    const int LW = int(std::ceil(tw * S)) + 2 * m + 2, LH = int(std::ceil(h * 1.7f * S)) + 2 * m;
    if (LW <= 0 || LH <= 0)
        return;
    std::vector<float> lay(size_t(LW) * size_t(LH) * 4, 0.0f);
    auto put = [&](int px, int py, uint32_t c, float a) {
        px -= LX, py -= LY;
        if (px < 0 || py < 0 || px >= LW || py >= LH)
            return;
        float* d = &lay[(size_t(py) * size_t(LW) + size_t(px)) * 4];
        const float k = a * (1.0f - d[3]);   // (under what is there: the letters do not overlap much)
        d[0] += float((c >> 16) & 255) * k, d[1] += float((c >> 8) & 255) * k, d[2] += float(c & 255) * k;
        d[3] += k;
    };
    int idx = 0;
    const std::vector<bool> tall = tallLetters(s);
    const std::string u = !tf ? upperPc(s) : lang::upperCase() ? upper(s) : s;
    if (tf) {   // lang: the language's font, its capitals h high on the same baseline, shaded top to bottom
        const float em = lang::pxForCap(h, tf) * S, top = y * S, hp = h * S;
        tf->draw(u, em, x * S, (y + h) * S, [&](int px, int py, float a) {
            if (distress > 0) {
                const float nz = vnoise(float(px) * 0.28f, float(py) * 0.28f, seed + 7);
                if (nz < distress * 0.42f) a *= 0.12f;
                else if (nz < distress * 0.5f) a *= 0.55f;
            }
            put(px, py, scale(rgb, 1.12f - 0.3f * clamp01((float(py) - top) / std::max(hp, 1.0f))), a);
        });
    } else
    for (size_t ci = 0; ci < u.size(); ++ci) {
        const char ch = u[ci];
        if (ch == ' ') {
            x += h * 0.42f;
            continue;
        }
        const float gh = tall[ci] ? h : h * 0.78f;
        const float f = gh / float(capH);
        const int g = glyph(uint8_t(ch), 0x10000);
        ++idx;
        if (!g) {
            x += h * 0.5f;
            continue;
        }
        const std::vector<uint8_t>& img = glyphImage(g);
        const int iw = spr_.width(g), ih = spr_.height(g);
        if (iw <= 0 || ih <= 0 || img.size() < size_t(iw) * size_t(ih) * 4) {
            x += h * 0.5f;
            continue;
        }
        // the glyph's box: its height scaled by f, its baseline on the capitals' (y + h)
        const float gx = float(ox) + (x - xs0) * S, gy = float(oy) + (h - float(ih) * f) * S;
        const int ow = int(std::ceil(float(iw) * f * S)), oh = int(std::ceil(float(ih) * f * S));
        const float inv = 1.0f / (f * S);
        const float jy = (rnd(idx, seed, 5) - 0.5f) * h * 0.03f * S;   // (a slight bounce: hand-made letters)
        for (int j = 0; j < oh; ++j)
            for (int i = 0; i < ow; ++i) {
                const float sx = (float(i) + 0.5f) * inv - 0.5f, sy = (float(j) + 0.5f) * inv - 0.5f;
                const int x0 = std::clamp(int(std::floor(sx)), 0, iw - 1), y0 = std::clamp(int(std::floor(sy)), 0, ih - 1);
                const int x1 = std::min(x0 + 1, iw - 1), y1 = std::min(y0 + 1, ih - 1);
                const float ax = clamp01(sx - float(x0)), ay = clamp01(sy - float(y0));
                auto at = [&](int xx, int yy) { return &img[(size_t(yy) * size_t(iw) + size_t(xx)) * 4]; };
                float a = 0, l = 0;
                const float wts[4] = {(1 - ax) * (1 - ay), ax * (1 - ay), (1 - ax) * ay, ax * ay};
                const uint8_t* ps[4] = {at(x0, y0), at(x1, y0), at(x0, y1), at(x1, y1)};
                for (int q = 0; q < 4; ++q) {
                    const float pa = ps[q][3] ? 1.0f : 0.0f;
                    a += wts[q] * pa;
                    l += wts[q] * pa * (0.3f * ps[q][0] + 0.59f * ps[q][1] + 0.11f * ps[q][2]) / 255.0f;
                }
                if (a <= 0.02f)
                    continue;
                l /= a;   // the font's own shading (its bevel and dark edge)
                const int px = int(gx) + i, py = int(gy + jy) + j;
                float alpha = a;
                if (distress > 0) {   // worn: holes and a ragged edge
                    const float nz = vnoise(float(px - ox) * 0.28f, float(py - oy) * 0.28f, seed + 7);   // (optui: from the text's origin)
                    if (nz < distress * 0.42f) alpha *= 0.12f;
                    else if (nz < distress * 0.5f) alpha *= 0.55f;
                }
                put(px, py, scale(rgb, 0.35f + 0.85f * l), alpha);
            }
        x += float(iw + 1) * f;
    }
    const size_t N = size_t(LW) * size_t(LH);
    std::vector<float> grow;   // the outline's coverage (or the letters' when there is none)
    if (fx & (kTxOutline | kTxShadow)) {
        grow.assign(N, 0.0f);
        // r: 0.75 units (1.5 canvas pixels; ~1.7 at 1080 lines), thinner for small text
        const float r = (fx & kTxOutline) ? std::max(1.0f, std::min(0.75f, h * 0.065f) * S) : 0.0f;
        const int ri = int(std::ceil(r + 0.5f));
        // menuperf: the disc's offsets and coverages worked out once (the strongest first: full coverage ends the
        // search), and a pixel with no letter within ri of it skipped (near: a box test, rows then columns, through
        // running counts): the same outline as testing every offset of every pixel, several times faster
        struct Off { int dx, dy; float cov; };
        std::vector<Off> ker;
        for (int dy = -ri; dy <= ri; ++dy)
            for (int dx = -ri; dx <= ri; ++dx)
                if (const float cov = clamp01(r + 0.5f - std::sqrt(float(dx * dx + dy * dy))); cov > 0)
                    ker.push_back({dx, dy, cov});
        std::stable_sort(ker.begin(), ker.end(), [](const Off& a, const Off& b) { return a.cov > b.cov; });
        std::vector<int> run(size_t(std::max(LW, LH)) + 1);
        std::vector<uint8_t> rowNear(N), near(N);
        for (int j = 0; j < LH; ++j) {
            for (int i = 0; i < LW; ++i)
                run[size_t(i) + 1] = run[size_t(i)] + (lay[(size_t(j) * size_t(LW) + size_t(i)) * 4 + 3] > 0.0f ? 1 : 0);
            for (int i = 0; i < LW; ++i)
                rowNear[size_t(j) * size_t(LW) + size_t(i)] = run[size_t(std::min(i + ri + 1, LW))] > run[size_t(std::max(i - ri, 0))];
        }
        for (int i = 0; i < LW; ++i) {
            for (int j = 0; j < LH; ++j)
                run[size_t(j) + 1] = run[size_t(j)] + rowNear[size_t(j) * size_t(LW) + size_t(i)];
            for (int j = 0; j < LH; ++j)
                near[size_t(j) * size_t(LW) + size_t(i)] = run[size_t(std::min(j + ri + 1, LH))] > run[size_t(std::max(j - ri, 0))];
        }
        forRows(0, LH, N, [&](int j) {   // (menuperf: rows on the helpers: the same pixels)
            for (int i = 0; i < LW; ++i) {
                float best = lay[(size_t(j) * size_t(LW) + size_t(i)) * 4 + 3];
                if (best < 1.0f && near[size_t(j) * size_t(LW) + size_t(i)])
                    for (const Off& o : ker) {
                        const int uu = i + o.dx, vv = j + o.dy;
                        if (uu < 0 || vv < 0 || uu >= LW || vv >= LH)
                            continue;
                        best = std::max(best, lay[(size_t(vv) * size_t(LW) + size_t(uu)) * 4 + 3] * o.cov);
                        if (best >= 1.0f)
                            break;
                    }
                grow[size_t(j) * size_t(LW) + size_t(i)] = best;
            }
        });
    }
    std::vector<float> b;   // the shadow's coverage
    if (fx & kTxShadow) {   // the shadow: the outline's shape moved down right, blurred (two box passes), 60 %
        const int ox = int(std::lround(0.6f * S)), oy = int(std::lround(1.0f * S)), br = std::max(1, int(std::lround(1.0f * S)));
        std::vector<float> t(N, 0.0f);
        b.assign(N, 0.0f);
        forRows(0, LH, N, [&](int j) {
            for (int i = 0; i < LW; ++i) {
                float sum = 0;
                for (int k = -br; k <= br; ++k) {
                    const int uu = i + k - ox, vv = j - oy;
                    if (uu >= 0 && vv >= 0 && uu < LW && vv < LH) sum += grow[size_t(vv) * size_t(LW) + size_t(uu)];
                }
                t[size_t(j) * size_t(LW) + size_t(i)] = sum / float(2 * br + 1);
            }
        });
        forRows(0, LH, N, [&](int j) {
            for (int i = 0; i < LW; ++i) {
                float sum = 0;
                for (int k = -br; k <= br; ++k)
                    if (j + k >= 0 && j + k < LH) sum += t[size_t(j + k) * size_t(LW) + size_t(i)];
                b[size_t(j) * size_t(LW) + size_t(i)] = sum / float(2 * br + 1);
            }
        });
    }
    // onto the text's own picture (optui: kept, then laid on the target), each pixel: the shadow, the outline, the
    // letter (the same order as three passes over all)
    TextTile tile;
    tile.dx = LX - ox, tile.dy = LY - oy, tile.w = LW, tile.h = LH;
    tile.rgba.assign(size_t(LW) * size_t(LH) * 4, 0);
    std::vector<uint8_t>* saveC = canvas_;
    const int sx = rOffX_, sy = rOffY_, sw = rW_, sh = rH_;
    canvas_ = &tile.rgba, rOffX_ = LX, rOffY_ = LY, rW_ = LW, rH_ = LH;
    struct Restore {   // (the target back however this ends)
        Frontend* f; std::vector<uint8_t>* c; int x, y, w, h;
        ~Restore() { f->canvas_ = c, f->rOffX_ = x, f->rOffY_ = y, f->rW_ = w, f->rH_ = h; }
    };
    {
    const Restore restore{this, saveC, sx, sy, sw, sh};
    forRows(0, LH, N, [&](int j) {
        for (int i = 0; i < LW; ++i) {
            const size_t p = size_t(j) * size_t(LW) + size_t(i);
            if (!b.empty() && b[p] > 0.01f)
                rBlend(LX + i, LY + j, 0x000000, b[p] * 0.6f);
            if ((fx & kTxOutline) && grow[p] > 0.01f)
                rBlend(LX + i, LY + j, 0x140807, grow[p] * 0.92f);
            const float* d = &lay[p * 4];
            if (d[3] > 0.01f)
                rBlend(LX + i, LY + j, (uint32_t(d[0] / d[3]) << 16) | (uint32_t(d[1] / d[3]) << 8) | uint32_t(d[2] / d[3]), d[3]);
        }
    });
    }
    blitText(tile);
    constexpr size_t kTextCacheBytes = size_t(96) << 20;
    if (textCacheBytes_ + tile.rgba.size() > kTextCacheBytes)
        textCache_.clear(), textCacheBytes_ = 0;
    textCacheBytes_ += tile.rgba.size();
    textCache_.emplace(tkey, std::move(tile));
}

// a picture scaled into (x, y, w, h) units (bilinear)
void Frontend::rImage(const std::vector<uint8_t>& img, int iw, int ih, float x, float y, float w, float h, float alpha)
{
    if (img.size() < size_t(iw) * size_t(ih) * 4 || w <= 0 || h <= 0)
        return;
    const float S = ps_;
    const int X0 = int(x * S), Y0 = int(y * S), W = int(w * S), H = int(h * S);
    for (int j = 0; j < H; ++j)
        for (int i = 0; i < W; ++i) {
            const float sx = (float(i) + 0.5f) * float(iw) / float(W) - 0.5f, sy = (float(j) + 0.5f) * float(ih) / float(H) - 0.5f;
            const int x0 = std::clamp(int(std::floor(sx)), 0, iw - 1), y0 = std::clamp(int(std::floor(sy)), 0, ih - 1);
            const int x1 = std::min(x0 + 1, iw - 1), y1 = std::min(y0 + 1, ih - 1);
            const float ax = clamp01(sx - float(x0)), ay = clamp01(sy - float(y0));
            float c[4] = {};
            const float wts[4] = {(1 - ax) * (1 - ay), ax * (1 - ay), (1 - ax) * ay, ax * ay};
            const int xs[4] = {x0, x1, x0, x1}, ys[4] = {y0, y0, y1, y1};
            for (int q = 0; q < 4; ++q) {
                const uint8_t* p = &img[(size_t(ys[q]) * size_t(iw) + size_t(xs[q])) * 4];
                const float pa = p[3] ? 1.0f : 0.0f;
                for (int k = 0; k < 3; ++k) c[k] += wts[q] * pa * p[k];
                c[3] += wts[q] * pa;
            }
            if (c[3] <= 0.01f)
                continue;
            const uint32_t rgb = (uint32_t(c[0] / c[3]) << 16) | (uint32_t(c[1] / c[3]) << 8) | uint32_t(c[2] / c[3]);
            rBlend(X0 + i, Y0 + j, rgb, c[3] * alpha);
        }
}

std::string Frontend::wordmarkFile;
std::string Frontend::promptIconDir;

// logo2: a picture with real (soft) alpha scaled into (x, y, w, h) units: each pixel the average of the source's
// pixels it covers (premultiplied; for the wordmark's outline / shadow, drawn smaller than its 3x art)
void Frontend::rImageSoft(const std::vector<uint8_t>& img, int iw, int ih, float x, float y, float w, float h)
{
    if (img.size() < size_t(iw) * size_t(ih) * 4 || w <= 0 || h <= 0)
        return;
    const float S = ps_;
    const int X0 = int(x * S), Y0 = int(y * S), W = int(w * S), H = int(h * S);
    if (W <= 0 || H <= 0)
        return;
    const float fx = float(iw) / float(W), fy = float(ih) / float(H);
    const int nx = std::max(1, int(std::ceil(fx))), ny = std::max(1, int(std::ceil(fy)));
    for (int j = 0; j < H; ++j)
        for (int i = 0; i < W; ++i) {
            float c[4] = {};
            for (int b = 0; b < ny; ++b)
                for (int a = 0; a < nx; ++a) {
                    const int sx = std::clamp(int((float(i) + (float(a) + 0.5f) / float(nx)) * fx), 0, iw - 1);
                    const int sy = std::clamp(int((float(j) + (float(b) + 0.5f) / float(ny)) * fy), 0, ih - 1);
                    const uint8_t* p = &img[(size_t(sy) * size_t(iw) + size_t(sx)) * 4];
                    const float pa = float(p[3]) / 255.0f;
                    for (int k = 0; k < 3; ++k) c[k] += pa * p[k];
                    c[3] += pa;
                }
            if (c[3] <= 0.001f)
                continue;
            const uint32_t rgb = (uint32_t(c[0] / c[3]) << 16) | (uint32_t(c[1] / c[3]) << 8) | uint32_t(c[2] / c[3]);
            rBlend(X0 + i, Y0 + j, rgb, c[3] / float(nx * ny));
        }
}

// a dark torn panel: ragged, burnt edges, a faint grey haze at the top, a red glow at the bottom (glow)
void Frontend::rTorn(float x, float y, float w, float h, int seed, bool glow, float alpha)
{
    const int m = 16;
    if (tileDepth_ == 0) {   // (on the target: through a tile drawn once)
        char key[96];
        std::snprintf(key, sizeof key, "T%g,%g,%g,%g,%d,%d,%g", x, y, w, h, seed, glow ? 1 : 0, alpha);
        rCached(key, x, y, w, h, float(m), [&]() { rTorn(x, y, w, h, seed, glow, alpha); });
        return;
    }
    const float S = ps_;
    const int X0 = std::max(0, int((x - m) * S)), Y0 = int((y - m) * S), X1 = std::min(int((x + w + m) * S), rpw()), Y1 = int((y + h + m) * S);
    std::vector<float> rts(size_t(std::max(0, X1 - X0))), rbs(rts.size());   // the top / bottom tears by column
    for (int px = X0; px < X1; ++px) {
        const float ux = float(px) / S;
        rts[size_t(px - X0)] = (fbm(ux * 0.06f, 7.7f, seed) - 0.5f) * 10.0f + (vnoise(ux * 0.7f, 8.0f, seed + 5) - 0.5f) * 2.5f;
        rbs[size_t(px - X0)] = (fbm(ux * 0.03f, 9.1f, seed) - 0.5f) * (glow ? 26.0f : 14.0f) + (vnoise(ux * 0.35f, 3.0f, seed + 9) - 0.5f) * 1.6f;
    }
    forRows(std::max(0, Y0), std::min(Y1, rph()), rts.size() * size_t(std::max(0, Y1 - Y0)), [&](int py) {   // (menuperf: rows on threads)
        const float uy = float(py) / S;
        const float rl =(fbm(uy * 0.07f, 1.3f, seed) - 0.5f) * 14.0f + (vnoise(uy * 0.6f, 2.0f, seed + 3) - 0.5f) * 3.0f;
        const float rr = (fbm(uy * 0.07f, 4.1f, seed) - 0.5f) * 14.0f + (vnoise(uy * 0.6f, 5.0f, seed + 3) - 0.5f) * 3.0f;
        for (int px = X0; px < X1; ++px) {
            const float ux = float(px) / S;
            const float rt = rts[size_t(px - X0)], rb = rbs[size_t(px - X0)];
            const float d = std::min({ux - x + rl, x + w - ux + rr, uy - y + rt, y + h - uy + rb});
            if (d <= 0)
                continue;
            const float edge = clamp01(d / 1.2f);
            const float tex = fbm(ux * 0.09f, uy * 0.09f, seed + 11);
            uint32_t c = mix(0x0E0A09, 0x2A2220, tex);
            // a grey haze under the top's middle
            const float hx = (ux - (x + w / 2)) / (w * 0.6f), hy = (uy - y) / (h * 0.7f);
            c = mix(c, 0x3A3532, clamp01(1.0f - std::sqrt(hx * hx + hy * hy)) * 0.45f);
            // burnt edges: darker near the tear
            c = mix(c, 0x060404, clamp01(1.0f - d / 7.0f) * 0.85f);
            if (glow) {   // the red glow rising from the bottom, brightest at its torn edge
                const float gy = clamp01((uy - (y + h * 0.62f)) / (h * 0.38f));
                const float nz = 0.55f + 0.45f * fbm(ux * 0.05f, uy * 0.05f, seed + 21);
                c = mix(c, tint(0x9A0A06), std::pow(gy, 1.5f) * nz);
                const float rim = clamp01(1.0f - (y + h + rb - uy) / 10.0f) * gy;
                c = mix(c, tint(0xD8200E), rim * 0.8f);
            }
            rBlend(px, py, c, edge * alpha);
        }
    });
}

// a torn parchment strip: off-white paper, stains, ragged ends, burnt edges, a slight tilt, a shadow under it
// (tone 0 lit, 1 normal, 2 grey)
void Frontend::rPaper(float x, float y, float w, float h, int seed, float tilt, int tone)
{
    if (tileDepth_ == 0) {   // (on the target: through a tile drawn once)
        char key[96];
        std::snprintf(key, sizeof key, "P%g,%g,%g,%g,%d,%g,%d", x, y, w, h, seed, tilt, tone);
        rCached(key, x, y, w, h, 14.0f, [&]() { rPaper(x, y, w, h, seed, tilt, tone); });
        return;
    }
    const float S = ps_;
    const float cx = x + w / 2;
    const int m = 12;
    const uint32_t base = tone == 0 ? 0xF2E9D6 : tone == 1 ? 0xD8CCB2 : 0x8E877E;
    for (int pass = 0; pass < 2; ++pass) {   // 0 the shadow (soft, down right onto the panel), 1 the paper
        const float ox = pass ? 0.0f : 1.5f, oy = pass ? 0.0f : 3.0f;
        const int X0 = int((x - m + ox) * S), Y0 = int((y - m + oy) * S), X1 = int((x + w + m + ox) * S), Y1 = int((y + h + m + oy) * S);
        // menuperf: what depends on the column only (the top / bottom tears, the lit strip's tinge), once a column
        const int CX0 = std::max(0, X0), CX1 = std::max(CX0, std::min(X1, rpw()));
        std::vector<float> colTo(size_t(CX1 - CX0)), colBo(colTo.size()), colTint(colTo.size());
        for (int px = CX0; px < CX1; ++px) {
            const float lx = (float(px) / S - ox) - x;
            colTo[size_t(px - CX0)] = (vnoise(lx * 0.12f, 2.0f, seed) - 0.5f) * 3.0f + (vnoise(lx * 0.6f, 3.0f, seed) - 0.5f) * 0.8f;
            colBo[size_t(px - CX0)] = (vnoise(lx * 0.12f, 9.0f, seed) - 0.5f) * 3.0f + (vnoise(lx * 0.6f, 4.0f, seed) - 0.5f) * 0.8f;
            colTint[size_t(px - CX0)] = tone == 0 ? fbm(lx * 0.1f, 3.0f, seed) : 0.0f;
        }
        forRows(std::max(0, Y0), std::min(Y1, rph()), size_t(CX1 - CX0) * size_t(std::max(0, Y1 - Y0)), [&](int py) {
            float rowLo = 0, rowRo = 0;   // (a strip without a tilt: the side tears depend on the row only)
            bool rowDone = false;
            for (int px = CX0; px < CX1; ++px) {
                const float ux = float(px) / S - ox, uy = float(py) / S - oy;
                const float lx = ux - x, ly = uy - y - (ux - cx) * tilt;
                // menuperf: the tears below reach at most 8.2 units in from the sides (lo, ro: -3 .. 8.2) and 1.9 from
                // the top / bottom (to, bo: -1.9 .. 1.9): farther out nothing is drawn (d < -3), farther in the edge
                // does not matter (d > 3: the full shadow, no burnt edge); those pixels skip the edge's noise (the
                // same picture, ~2.5 times faster: the paper strips were 10-13 ms each at 1080 lines)
                if (lx < -6.5f || lx > w + 6.5f || ly < -5.2f || ly > h + 5.2f)
                    continue;
                float d = 3.0f;
                if (!(lx > 11.5f && lx < w - 11.5f && ly > 5.2f && ly < h - 5.2f)) {
                    if (!rowDone || tilt != 0.0f) {
                        rowLo = (fbm(ly * 0.22f, 1.0f, seed) - 0.3f) * 10.0f + vnoise(ly * 0.9f, 3.0f, seed) * 1.2f;
                        rowRo = (fbm(ly * 0.22f, 6.0f, seed) - 0.3f) * 10.0f + vnoise(ly * 0.9f, 4.0f, seed) * 1.2f;
                        rowDone = true;
                    }
                    const float lo = rowLo, ro = rowRo;
                    const float to = colTo[size_t(px - CX0)], bo = colBo[size_t(px - CX0)];
                    d = std::min({lx - lo, w - lx - ro, ly - to, h - ly - bo});
                }
                if (!pass) {   // (a 3-unit soft edge, the paper's ragged shape)
                    if (d > -3.0f) {
                        const float t = clamp01((d + 3.0f) / 6.0f);
                        rBlend(px, py, 0x000000, t * t * (3 - 2 * t) * 0.62f);
                    }
                    continue;
                }
                if (d <= 0)
                    continue;
                const float a = clamp01(d / 0.8f);
                const float st = fbm(lx * 0.07f, ly * 0.16f, seed + 40);
                uint32_t c = scale(base, 1.04f - 0.24f * st);
                c = mix(c, 0x8A5A30, clamp01((st - 0.64f) * 2.0f) * 0.4f);              // brown stains
                c = mix(c, 0x3A2010, clamp01(1.0f - d / 3.0f) * 0.7f);                   // the burnt edge
                if (rnd(int(lx * 1.5f), int(ly * 1.5f), seed + 3) < 0.0025f)
                    c = mix(c, 0x40281A, 0.4f);                                              // a few specks
                if (tone == 0)   // the lit strip: a red tinge creeping in from the bottom
                    c = mix(c, 0xC8382A, clamp01((ly - h * 0.65f) / (h * 0.5f)) * 0.35f * colTint[size_t(px - CX0)]);
                rBlend(px, py, c, a);
            }
        });
    }
}

// ---- the blood: the arcade's hit marks (remake2) ----
void Frontend::setArcadeRegions(const std::string& dir)
{
    if (dir == regionsDir_)
        return;
    regionsDir_ = dir;
    if (bloodTried_ && !bloodArcade_ && !dir.empty()) {   // (the PC's copies so far: the arcade's from now on)
        bloodTried_ = false;
        splatW_ = 0;
        tiles_.clear();
        staticSig_.clear();
        bgKey_ = -1;
    }
}

// the hit marks (hud_player_notice_show 0x945D0's table 0xCA5FC: sprites 0x28..0x2D, docs/teaser_status/display2.md
// §2) from the arcade's program ROM / data (the sheet table 0xA6590, tools/sprites_tool.py arcade_sprite), else the
// PC's matching sprites (0x47E..0x483). Kept as coverage + shade (the palette's brightness, 0..255 over the sprite's
// brightest): the drawing colours them in one blood red ramp (some of the arcade's marks have their red set at run time)
void Frontend::loadBloodArt()
{
    if (bloodTried_)
        return;
    bloodTried_ = true;
    bloodArcade_ = false;
    for (BloodArt& b : bloodArt_)
        b = BloodArt{};
    auto fromRgba = [](BloodArt& b, const std::vector<uint8_t>& px, int w, int h) {
        if (w <= 0 || h <= 0 || px.size() < size_t(w) * size_t(h) * 4)
            return;
        b.w = w, b.h = h;
        b.a.assign(size_t(w) * size_t(h), 0);
        b.l.assign(b.a.size(), 0);
        float top = 1;
        for (size_t i = 0; i < b.a.size(); ++i)
            if (px[i * 4 + 3])
                top = std::max(top, 0.3f * px[i * 4] + 0.59f * px[i * 4 + 1] + 0.11f * px[i * 4 + 2]);
        for (size_t i = 0; i < b.a.size(); ++i)
            if (px[i * 4 + 3]) {
                b.a[i] = 255;
                b.l[i] = uint8_t(std::clamp((0.3f * px[i * 4] + 0.59f * px[i * 4 + 1] + 0.11f * px[i * 4 + 2]) / top * 255.0f, 0.0f, 255.0f));
            }
    };
    if (!regionsDir_.empty()) {
        auto readAll = [](const std::string& p) {
            std::vector<uint8_t> v;
            if (std::FILE* f = std::fopen(p.c_str(), "rb")) {
                std::fseek(f, 0, SEEK_END);
                const long n = std::ftell(f);
                std::fseek(f, 0, SEEK_SET);
                v.resize(n > 0 ? size_t(n) : 0);
                if (std::fread(v.data(), 1, v.size(), f) != v.size()) v.clear();
                std::fclose(f);
            }
            return v;
        };
        const std::vector<uint8_t> rom = readAll(regionsDir_ + "/maincpu.bin"), data = readAll(regionsDir_ + "/main_data.bin");
        // an address -> the byte there (0: none): the program ROM below 0x200000, main_data at 0x2000000 / 0x6000000
        auto at = [&](uint32_t a, size_t n) -> const uint8_t* {
            const std::vector<uint8_t>* b = nullptr;
            size_t o = 0;
            if (a < 0x200000) b = &rom, o = a;
            else if (a >= 0x2000000 && a < 0x4000000) b = &data, o = a - 0x2000000;
            else if (a >= 0x6000000 && a < 0x7000000) b = &data, o = a - 0x5000000;
            return b && o + n <= b->size() ? b->data() + o : nullptr;
        };
        auto u32 = [&](uint32_t a) { const uint8_t* p = at(a, 4); return p ? uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24 : 0u; };
        auto u16 = [&](uint32_t a) { const uint8_t* p = at(a, 2); return p ? uint16_t(p[0] | p[1] << 8) : uint16_t(0); };
        int got = 0;
        for (int k = 0; k < 6 && rom.size() >= 0x100000; ++k) {
            const uint32_t sheet = 0xA6590 + uint32_t(0x28 + k) * 12;
            const uint32_t t0 = u32(sheet), p0 = u32(sheet + 4), lay = u32(sheet + 8);
            std::map<int, const uint8_t*> tiles;   // tile number -> its 32 bytes
            std::map<int, uint32_t> pals;          // slot -> its palette's address
            uint32_t t = t0;
            int base = 0, piece = 0;
            for (int guard = 0; guard < 64 && t; ++guard) {
                const uint32_t n = u32(t);
                if (n == 0 || n > 0x4000 || !at(t + 4, size_t(n) * 32))
                    break;
                for (uint32_t i = 0; i < n; ++i)
                    tiles[base + int(i)] = at(t + 4 + i * 32, 32);
                for (int s = 0; s < int((n + 127) / 128); ++s)
                    pals[(base >> 7) + s] = p0 + uint32_t(piece) * 32;
                base += int((n + 127) / 128) * 128;
                t += 4 + n * 32;
                ++piece;
            }
            const int cw = u16(lay), ch = u16(lay + 2);
            if (tiles.empty() || cw <= 0 || ch <= 0 || cw > 62 || ch > 48)
                continue;
            const int w = cw * 8, h = ch * 8;
            std::vector<uint8_t> px(size_t(w) * size_t(h) * 4, 0);
            for (int cy = 0; cy < ch; ++cy)
                for (int cx = 0; cx < cw; ++cx) {
                    const int tn = u16(lay + 4 + 2 * uint32_t(cy * cw + cx)) & 0x3FFF;
                    const auto ti = tiles.find(tn);
                    const auto pi = pals.find(tn >> 7);
                    if (ti == tiles.end() || pi == pals.end())
                        continue;
                    for (int y = 0; y < 8; ++y)
                        for (int x = 0; x < 8; ++x) {
                            const uint8_t* row = ti->second + y * 4;
                            const uint16_t wd = uint16_t(row[(x >> 2) * 2] | row[(x >> 2) * 2 + 1] << 8);
                            const int pen = (wd >> (12 - 4 * (x & 3))) & 15;
                            const uint16_t col = u16(pi->second + uint32_t(pen) * 2);
                            if (pen == 0 || (col & 0x7FFF) == 0)
                                continue;
                            uint8_t* d = &px[(size_t(cy * 8 + y) * size_t(w) + size_t(cx * 8 + x)) * 4];
                            d[0] = uint8_t((col & 31) * 255 / 31), d[1] = uint8_t((col >> 5 & 31) * 255 / 31);
                            d[2] = uint8_t((col >> 10 & 31) * 255 / 31), d[3] = 255;
                        }
                }
            fromRgba(bloodArt_[k], px, w, h);
            got += bloodArt_[k].w > 0;
        }
        bloodArcade_ = got == 6;
    }
    if (!bloodArcade_)
        for (int k = 0; k < 6; ++k)
            fromRgba(bloodArt_[k], spr_.image(0x47E + k), spr_.width(0x47E + k), spr_.height(0x47E + k));
}

// one hit mark (rBlood's header comment), bilinear, in a dark-to-bright blood ramp by the art's shade
void Frontend::rBlood(int k, float cx, float cy, float hgt, float ang, float alpha, bool flip, float sx, float bright)
{
    loadBloodArt();
    const BloodArt& b = bloodArt_[k];
    if (b.w <= 0 || hgt <= 0)
        return;
    const float S = ps_;
    const float sc = hgt * S / float(b.h);   // canvas pixels a texel (down), sc * sx across
    const float ca = std::cos(ang), sa = std::sin(ang);
    const float hw = float(b.w) * sc * sx / 2, hh = float(b.h) * sc / 2;
    const float ext = std::fabs(ca) * hw + std::fabs(sa) * hh, eyt = std::fabs(sa) * hw + std::fabs(ca) * hh;
    const float pcx = cx * S, pcy = cy * S;
    for (int py = std::max(0, int(pcy - eyt) - 1); py < std::min(int(pcy + eyt) + 2, rph()); ++py)
        for (int px = std::max(0, int(pcx - ext) - 1); px < std::min(int(pcx + ext) + 2, rpw()); ++px) {
            const float dx = float(px) + 0.5f - pcx, dy = float(py) + 0.5f - pcy;
            float u = (dx * ca + dy * sa) / (sc * sx) + float(b.w) / 2, v = (-dx * sa + dy * ca) / sc + float(b.h) / 2;
            if (flip) u = float(b.w) - u;
            u -= 0.5f, v -= 0.5f;
            if (u < -1 || v < -1 || u > float(b.w) || v > float(b.h))
                continue;
            const int x0 = int(std::floor(u)), y0 = int(std::floor(v));
            const float ax = u - float(x0), ay = v - float(y0);
            float a = 0, l = 0;
            for (int q = 0; q < 4; ++q) {
                const int xx = x0 + (q & 1), yy = y0 + (q >> 1);
                if (xx < 0 || yy < 0 || xx >= b.w || yy >= b.h)
                    continue;
                const float wq = ((q & 1) ? ax : 1 - ax) * ((q >> 1) ? ay : 1 - ay);
                const size_t i = size_t(yy) * size_t(b.w) + size_t(xx);
                a += wq * b.a[i] / 255.0f;
                l += wq * b.a[i] / 255.0f * b.l[i] / 255.0f;
            }
            if (a <= 0.01f)
                continue;
            l /= a;
            rBlend(px, py, tint(scale(mix(0x3A0302, 0xA00C08, std::pow(l, 0.8f)), bright)), clamp01(a) * alpha);
        }
}

// the handprint (a switch: On bloody red, Off faint): a palm and five fingers
void Frontend::rHand(float cx, float cy, float s, bool on)
{
    const float S = ps_;
    struct Seg { float ax, ay, bx, by, r; };
    const Seg segs[6] = {
        {cx - s * 0.02f, cy + s * 0.12f, cx + s * 0.02f, cy + s * 0.2f, s * 0.24f},   // the palm
        {cx - s * 0.17f, cy - s * 0.05f, cx - s * 0.26f, cy - s * 0.38f, s * 0.065f},
        {cx - s * 0.06f, cy - s * 0.08f, cx - s * 0.08f, cy - s * 0.47f, s * 0.07f},
        {cx + s * 0.06f, cy - s * 0.08f, cx + s * 0.08f, cy - s * 0.45f, s * 0.07f},
        {cx + s * 0.17f, cy - s * 0.03f, cx + s * 0.24f, cy - s * 0.32f, s * 0.06f},
        {cx - s * 0.2f, cy + s * 0.14f, cx - s * 0.42f, cy - s * 0.02f, s * 0.07f}};  // the thumb
    const int X0 = int((cx - s * 0.6f) * S), X1 = int((cx + s * 0.6f) * S), Y0 = int((cy - s * 0.6f) * S), Y1 = int((cy + s * 0.6f) * S);
    for (int py = std::max(0, Y0); py < std::min(Y1, rph()); ++py)
        for (int px = std::max(0, X0); px < std::min(X1, rpw()); ++px) {
            const float ux = float(px) / S, uy = float(py) / S;
            float best = 1e9f;
            for (const Seg& g : segs) {
                const float vx = g.bx - g.ax, vy = g.by - g.ay, l2 = vx * vx + vy * vy;
                const float t = l2 > 0 ? clamp01(((ux - g.ax) * vx + (uy - g.ay) * vy) / l2) : 0.0f;
                const float dx = ux - (g.ax + vx * t), dy = uy - (g.ay + vy * t);
                best = std::min(best, std::sqrt(dx * dx + dy * dy) - g.r);
            }
            const float nz = vnoise(ux * 0.9f, uy * 0.9f, 61);
            if (best > 0.4f || nz < (on ? 0.18f : 0.3f))
                continue;
            const float a = clamp01((0.4f - best) / 0.8f) * (on ? 0.95f : 0.28f);
            rBlend(px, py, on ? tint(mix(0x7C0604, 0xC81A10, nz)) : 0xB8B0A8, a);
        }
}

// a prompt's key cap: a soft shadow, a dark translucent body, a crisp light border one unit wide (ring: a circle)
void Frontend::rKeyCap(float x, float y, float w, float h, bool ring)
{
    const float S = ps_;
    const float cx = x + w / 2, cy = y + h / 2, rad = ring ? h / 2 : 3.0f;
    // the signed distance (units) to the cap's outline: a rounded box (radius rad) / a circle
    auto sd = [&](float ux, float uy) {
        if (ring)
            return std::hypot(ux - cx, uy - cy) - h / 2;
        const float qx = std::fabs(ux - cx) - (w / 2 - rad), qy = std::fabs(uy - cy) - (h / 2 - rad);
        return std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f)) + std::min(std::max(qx, qy), 0.0f) - rad;
    };
    for (int py = std::max(0, int((y - 4) * S)); py < std::min(int((y + h + 6) * S), rph()); ++py)
        for (int px = std::max(0, int((x - 4) * S)); px < std::min(int((x + w + 6) * S), rpw()); ++px) {
            const float ux = (float(px) + 0.5f) / S, uy = (float(py) + 0.5f) / S;
            const float sh = sd(ux - 0.8f, uy - 1.4f);   // the shadow
            if (sh < 2.0f)
                rBlend(px, py, 0x000000, clamp01((2.0f - sh) / 4.0f) * 0.7f);
            const float d = sd(ux, uy);
            const float px1 = 1.0f / S;   // (a pixel in units: the antialiased edge)
            if (d < px1)
                rBlend(px, py, 0x120A09, clamp01((px1 - d) / px1) * 0.78f);
            const float bd = std::fabs(d + 0.5f);   // the border: one unit inside the outline
            if (bd < 0.5f + px1)
                rBlend(px, py, 0xECE4DC, clamp01((0.5f + px1 - bd) / px1) * 0.95f);
        }
}

// padpass: the prompts' pad keys as the last pad's type names them (keybinds.h padInputName: SDL's labels): an Xbox
// pad A / B / LB, a PlayStation pad Cross / Circle / L1 (drawn as its shapes), a Nintendo pad B / A / L (SDL's South
// is the Nintendo's B)
std::string Frontend::padKey(const std::string& xbox) const
{
    static const struct { const char* x; int in; } kMap[] = {
        {"A", kPiSouth}, {"B", kPiEast}, {"X", kPiWest}, {"Y", kPiNorth}, {"LB", kPiL1}, {"RB", kPiR1}, {"LT", kPiL2},
        {"RT", kPiR2}, {"LS", kPiL3}, {"RS", kPiR3}, {"Start", kPiStart}, {"View", kPiBack}, {"Back", kPiBack}};
    std::string out;
    size_t at = 0;
    while (at <= xbox.size()) {
        const size_t slash = std::min(xbox.find('/', at), xbox.size());
        const std::string t = xbox.substr(at, slash - at);
        std::string n = t;
        for (const auto& m : kMap)
            if (t == m.x) n = padInputName(m.in, padType_);
        out += (at ? "/" : "") + n;
        at = slash + 1;
    }
    return out;
}

bool Frontend::ringKey(const std::string& key) const
{
    return padPrompts_ && (key.size() == 1 || key == "Cross" || key == "Circle" || key == "Square" || key == "Triangle" ||
                           key == "Left" || key == "Down" || key == "Up" || key == "Right");
}

// padglyphs: the icons (Kenney's Input Prompts, CC0: data/ui/prompts), read once each (null: missing)
namespace {
struct PromptIcon {
    int w = 0, h = 0;
    std::vector<uint8_t> px;
    bool ok = false;
};
const PromptIcon& promptIcon(const std::string& path)
{
    static std::mutex mu;
    static std::map<std::string, PromptIcon> cache;
    std::lock_guard<std::mutex> lk(mu);
    auto it = cache.find(path);
    if (it != cache.end())
        return it->second;
    PromptIcon ic;
    std::string err;
    ic.ok = readPngRgba(path, ic.w, ic.h, ic.px, err) && ic.w > 0 && ic.h > 0;
    if (!ic.ok && !err.empty()) std::fprintf(stderr, "prompt icon: %s\n", err.c_str());
    return cache.emplace(path, std::move(ic)).first->second;
}
} // namespace

std::vector<std::string> Frontend::padIconsOf(const std::string& key) const
{
    // each part of "L1/R1" (a pad's) or "T/Esc" (the keys') with its icon: the Button Style's pack, then (Xelu)
    // Kenney's; any part without one: none (the cap and the text instead)
    std::vector<std::string> out;
    if (promptIconDir.empty() || buttonStyle >= kPackSimple)
        return out;
    size_t at = 0;
    while (at <= key.size()) {
        const size_t slash = std::min(key.find('/', at), key.size());
        const std::string t = key.substr(at, slash - at);
        std::string ic;
        for (int pack = buttonStyle; pack <= kPackKenney && ic.empty(); ++pack) {
            std::string c;
            if (!padPrompts_)
                c = keyIcon(pack, t);
            else
                for (int i = 0; i < kPadInputs && c.empty(); ++i)
                    if (padInputName(i, padType_) == t) c = padIcon(pack, padType_, i);
            if (!c.empty() && promptIcon(promptIconDir + "/" + c + ".png").ok) ic = c;
            if (!padPrompts_ && pack == kPackXelu) break;   // (Kenney's keys: its own style only)
        }
        if (ic.empty())
            return {};
        out.push_back(ic);
        at = slash + 1;
    }
    return out;
}

bool Frontend::rPadGlyph(const std::string& key, float cx, float cy, float r)
{
    const std::vector<std::string> icons = padIconsOf(key);
    if (icons.empty())
        return false;
    const float S = ps_, pitch = 2.0f * r;
    for (size_t k = 0; k < icons.size(); ++k) {
        const std::string& ic = icons[k];
        // (both packs draw the button with a margin round it: a little larger than the cap; Kenney's 128 px one past
        // 64 screen pixels; Xelu's keep their coloured letters, Kenney's white warmed to the menu's ink)
        const bool kenney = ic.rfind("kenney/", 0) == 0;
        const float side = 2.0f * r * (kenney ? 1.3f : 1.25f);
        const float x0 = cx - pitch * float(icons.size() - 1) / 2 - side / 2;
        const size_t sl = ic.find('/', 7);
        const PromptIcon* im = &promptIcon(promptIconDir + "/" + ic + ".png");
        if (kenney && side * S > 64.0f && sl != std::string::npos)
            if (const PromptIcon& d = promptIcon(promptIconDir + "/" + ic.substr(0, sl) + "/double" + ic.substr(sl) + ".png"); d.ok) im = &d;
        const uint32_t ink = kenney ? 0xF4ECE4 : 0xFFFFFF;
        // prerel (user 2026-10-27: Xelu's icons read dim on the dark red legend and pages): Xelu's dark grey buttons
        // get a thin light rim (the icon's own shape a little larger, in the menu's ink, under it) and are lifted a
        // little toward white (lift); Kenney's white ones stay as they were
        // menus3 (user 2026-10-28: "I personally liked the normal version better"): only with Controls > Icon Outline
        // On (iconOutline; default Off: Xelu's icons as they are)
        const bool rim = !kenney && iconOutline;
        const float lift = rim ? 0.28f : 0.0f;
        // feat29 (user 2026-10-29: "Add shadow to Switch 1 controls"): Xelu's Switch buttons are grey letters on dark
        // grey (the Xbox ones' letters are coloured, the PlayStation ones light): a drop shadow under them (pass -1)
        // and their light parts (the letters, the rims) brightened toward the menu's ink so they read as the others
        const bool sw1 = ic.rfind("xelu/switch/", 0) == 0;
        // box-filtered down (n x n samples a pixel)
        const float cbx = x0 + pitch * float(k) + side / 2;
        for (int pass = sw1 ? -1 : rim ? 0 : 1; pass < 2; ++pass) {
            if (pass == 0 && !rim) continue;
            const float sd = pass == 0 ? side * 1.14f : side;   // (pass 0: the rim)
            const float sh = pass < 0 ? side * 0.07f : 0.0f;    // (pass -1: the shadow, down and right)
            const float bx = cbx - sd / 2 + sh, by = cy - sd / 2 + sh, sc = float(im->w) / (sd * S);
            const int n = std::clamp(int(std::ceil(sc)), 1, 4);
            for (int py = std::max(0, int(by * S)); py < std::min(int((by + sd) * S) + 1, rph()); ++py)
                for (int px = std::max(0, int(bx * S)); px < std::min(int((bx + sd) * S) + 1, rpw()); ++px) {
                    float ar = 0, ag = 0, ab = 0, aa = 0;
                    for (int sy = 0; sy < n; ++sy)
                        for (int sx = 0; sx < n; ++sx) {
                            const float ux = ((float(px) + (float(sx) + 0.5f) / float(n)) / S - bx) / sd;
                            const float uy = ((float(py) + (float(sy) + 0.5f) / float(n)) / S - by) / sd;
                            if (ux < 0 || uy < 0 || ux >= 1 || uy >= 1) continue;
                            const uint8_t* p = &im->px[(size_t(int(uy * float(im->h))) * size_t(im->w) + size_t(int(ux * float(im->w)))) * 4];
                            const float a = float(p[3]) / 255.0f;
                            ar += float(p[0]) * a, ag += float(p[1]) * a, ab += float(p[2]) * a, aa += a;
                        }
                    if (aa <= 0) continue;
                    if (pass < 0) {
                        rBlend(px, py, 0x000000, 0.6f * aa / float(n * n));
                        continue;
                    }
                    if (pass == 0) {
                        rBlend(px, py, 0xE8DCD0, 0.8f * aa / float(n * n));
                        continue;
                    }
                    if (sw1) {   // (the light parts: 100 -> 100, 160 -> 240)
                        auto up = [](float v) { return v > 100.0f ? std::min(255.0f, 100.0f + (v - 100.0f) * 2.3f) : v; };
                        ar = up(ar / aa) * aa, ag = up(ag / aa) * aa, ab = up(ab / aa) * aa;
                    }
                    auto ch = [lift](float v, uint32_t inkc) { const float c = v * float(inkc) / 255.0f; return uint32_t(c + (255.0f - c) * lift); };
                    const uint32_t col = ch(ar / aa, ink >> 16) << 16 | ch(ag / aa, (ink >> 8) & 255) << 8 | ch(ab / aa, ink & 255);
                    rBlend(px, py, col, aa / float(n * n));
                }
        }
    }
    return true;
}

bool Frontend::rPadSymbol(const std::string& key, float cx, float cy, float r)
{
    if (key == "Left" || key == "Down" || key == "Up" || key == "Right") {   // padglyphs: a Joy-Con's arrow, filled
        const float S = ps_, s = r * 0.42f;
        for (int py = std::max(0, int((cy - r) * S)); py < std::min(int((cy + r) * S) + 1, rph()); ++py)
            for (int px = std::max(0, int((cx - r) * S)); px < std::min(int((cx + r) * S) + 1, rpw()); ++px) {
                float ux = (float(px) + 0.5f) / S - cx, uy = (float(py) + 0.5f) / S - cy;
                if (key == "Down") uy = -uy;
                else if (key == "Left") std::swap(ux, uy);
                else if (key == "Right") std::swap(ux, uy), uy = -uy;
                // pointing up: the apex (0, -s), the base at y = 0.6 s, half as wide as tall
                const float d = std::max(uy - 0.6f * s, (std::fabs(ux) * 1.6f - (uy + s)) / 1.887f);
                const float a = clamp01((0.5f / S - d) * S);
                if (a > 0) rBlend(px, py, 0xF4ECE4, a);
            }
        return true;
    }
    const int k = key == "Cross" ? 0 : key == "Circle" ? 1 : key == "Square" ? 2 : key == "Triangle" ? 3 : -1;
    if (k < 0)
        return false;
    // the shape's outline (its signed distance in units), a stroke about 0.9 units wide, in the pad's colours softened
    static const uint32_t kCol[4] = {0x9CC0F4, 0xF49C9C, 0xEAAAD8, 0x9CE6CE};
    const float S = ps_, s = r * 0.46f, half = 0.45f;
    for (int py = std::max(0, int((cy - r) * S)); py < std::min(int((cy + r) * S) + 1, rph()); ++py)
        for (int px = std::max(0, int((cx - r) * S)); px < std::min(int((cx + r) * S) + 1, rpw()); ++px) {
            const float ux = (float(px) + 0.5f) / S - cx, uy = (float(py) + 0.5f) / S - cy;
            float d;
            if (k == 0) {   // two diagonals
                const float sx = std::clamp((ux + uy) * 0.5f, -s, s), sy = std::clamp((ux - uy) * 0.5f, -s, s);
                d = std::min(std::hypot(ux - sx, uy - sx), std::hypot(ux - sy, uy + sy));
            } else if (k == 1) {
                d = std::fabs(std::hypot(ux, uy) - s);
            } else if (k == 2) {
                d = std::fabs(std::max(std::fabs(ux), std::fabs(uy)) - s * 0.85f);
            } else {   // an equilateral triangle pointing up (its centroid a little below the middle)
                const float kq = 1.7320508f, tr = s * 1.05f;
                float qx = std::fabs(ux) - tr, qy = -(uy - s * 0.12f) + tr / kq;
                if (qx + kq * qy > 0) {
                    const float nx = (qx - kq * qy) / 2, ny = (-kq * qx - qy) / 2;
                    qx = nx, qy = ny;
                }
                qx -= std::clamp(qx, -2 * tr, 0.0f);
                d = std::hypot(qx, qy);
            }
            const float a = clamp01((half + 0.5f / S - d) * S);
            if (a > 0)
                rBlend(px, py, kCol[k], a);
        }
    return true;
}

void Frontend::rChevron(float cx, float cy, float s, uint32_t rgb, float alpha)
{
    const float S = ps_;
    const int X0 = int((cx - s) * S), X1 = int((cx + s) * S), Y0 = int((cy - s) * S), Y1 = int((cy + s) * S);
    for (int py = std::max(0, Y0); py < Y1; ++py)
        for (int px = std::max(0, X0); px < X1; ++px) {
            const float ux = float(px) / S - cx, uy = float(py) / S - cy;
            // a V (pointing down) of two strokes, y = 0.2 s - 0.75 |x|; s < 0: pointing up (mirrored)
            const float as = std::fabs(s), ay = s > 0 ? uy : -uy;
            const float line = std::fabs(ay + std::fabs(ux) * 0.75f - as * 0.2f);
            if (std::fabs(ux) <= as * 0.62f && line < as * 0.16f)
                rBlend(px, py, rgb, clamp01((as * 0.16f - line) * S) * alpha);
        }
}

// ---- the blood colour (the Blood Color setting) ----
uint32_t Frontend::bloodColour() const
{
    if (bloodRgb >= 0) {   // the custom colour: its brightest channel at the red's (0xA0 + a little)
        const int r = (bloodRgb >> 16) & 255, g = (bloodRgb >> 8) & 255, b = bloodRgb & 255, m = std::max({r, g, b});
        if (m < 8)
            return 0x2A2626;
        const float k = 176.0f / float(m);
        return uint32_t(float(r) * k) << 16 | uint32_t(float(g) * k) << 8 | uint32_t(float(b) * k);
    }
    // red, green, blue, purple, yellow (the PC's), as deep as the red
    static const uint32_t kHue[5] = {0xA00C08, 0x1E8A12, 0x1630B4, 0x7C129E, 0xB09A10};
    return kHue[std::clamp(options.blood, 0, 4)];
}

uint32_t Frontend::tint(uint32_t c) const
{
    if (bloodHue_ == 0xA00C08)
        return c;
    // a red = its white part (min of green, blue) + an amount of red: the amount laid on the blood colour instead
    const float r = float((c >> 16) & 255), g = float((c >> 8) & 255), b = float(c & 255), wh = std::min(g, b);
    const float amt = std::max(0.0f, r - wh) / 160.0f;
    auto ch = [&](int sh) { return uint32_t(std::clamp(float((bloodHue_ >> sh) & 255) * amt + wh, 0.0f, 255.0f)) << sh; };
    return ch(16) | ch(8) | ch(0);
}

// blood2: the settings pages' backdrop tint (after its grey and dark: c * mul + add), in the blood colour. The red is the
// old one exactly (red x 1.5 + 14, green and blue x 0.55); another colour: the same white part (x 0.55) and the red's
// excess (x 0.95 + 14 of 160) laid on that colour instead, as tint()
void Frontend::backdropTint(bool on, float mul[3], float add[3]) const
{
    // (the main menu, user 2026-10-21 "the video in the background also reflects the blood colour": the same tint at
    // 30 %, a faint cast; the settings pages: whole)
    const float k = on ? 1.0f : 0.3f;
    for (int c = 0; c < 3; ++c) {
        float m, a;
        if (bloodHue_ == 0xA00C08)
            m = c == 0 ? 1.5f : 0.55f, a = c == 0 ? 14.0f : 0.0f;
        else {
            const float h = float((bloodHue_ >> (16 - 8 * c)) & 255) / 160.0f;
            m = 0.55f + 0.95f * h, a = 14.0f * h;
        }
        mul[c] = 1.0f + (m - 1.0f) * k, add[c] = a * k;
    }
}

uint32_t Frontend::paperInk(uint32_t c) const
{
    c = tint(c);
    const float l = 0.3f * float((c >> 16) & 255) + 0.59f * float((c >> 8) & 255) + 0.11f * float(c & 255);
    return l > 105.0f ? scale(c, 105.0f / l) : c;   // (yellow on the cream paper: darkened to read)
}

void Frontend::setOutputHeight(int h)
{
    // min(h, 1440) lines, in quarter units, at least 2 (the old 960 lines): a change draws the layers again (their keys
    // hold the pixel size)
    ps_ = std::max(2.0f, std::ceil(float(std::clamp(h, 1, 1440)) / float(kHeight) * 4.0f) / 4.0f);
}

void Frontend::newSplatter()
{
    uint32_t s = splatSeed;
    for (int k = 0; s == splatSeed || s == 0; ++k)
        s = hash3(int(splatSeed), int(nowMs_), 977 + k);
    splatSeed = s;
    if (onCue)
        onCue(32);   // a wet hit (the arcade's blood burst 0xA0120A as the PC's cue)
    if (onSplatSeed)
        onSplatSeed(s);
}

// ---- the blood splatter (blood2, user 2026-10-21: the generated splatter back, made to look like real blood) ----
// Heavy at the top corners and down the sides, thinning towards the middle (the Remake's arrangement, our own shapes):
// smeared stains in the corners, blobs with lobes, directional spray (drops stretched away from where they came from)
// and runs from the top that thin out and end in a bead. All of it is a thickness field (cover, height, gloss, tone
// per canvas pixel) shaded once: thin = lighter and see-through, thick = dark red to near black, a wet highlight on
// the larger blobs from the height's slope. Units: 640 x 480 high (cw_ wide); drawn at the canvas size.
namespace {
// blood2: the splatter's drawing as a plain function (no Frontend state: it also runs on a worker thread for the GPU
// menu's splatter layer): the screen's width in units, pixels a unit, the pattern's seed, the blood colour
struct SplatJob {
    int cw = Frontend::kWidth;
    float ps = 2.0f;
    uint32_t seed = 0, hue = 0xA00C08;
};
// Frontend::tint for a given blood colour
uint32_t tintHue(uint32_t hue, uint32_t c)
{
    if (hue == 0xA00C08)
        return c;
    const float r = float((c >> 16) & 255), g = float((c >> 8) & 255), b = float(c & 255), wh = std::min(g, b);
    const float amt = std::max(0.0f, r - wh) / 160.0f;
    auto ch = [&](int sh) { return uint32_t(std::clamp(float((hue >> sh) & 255) * amt + wh, 0.0f, 255.0f)) << sh; };
    return ch(16) | ch(8) | ch(0);
}

template <class Out>
void splatterDraw(const SplatJob& J, int cx0, int cy0, int cx1, int cy1, Out&& out)
{
    const float S = J.ps, W = float(J.cw), H = float(Frontend::kHeight);
    // blood2: the pattern's seed (splatSeed; 0 the first pattern): the noise families shifted, the blobs moved a little
    // around their places (the framing rules stay: every seed frames the panel)
    const int so = int(J.seed & 0xFFFF) * 131, jit = J.seed ? 1 : 0;
    cx0 = std::max(cx0, 0), cy0 = std::max(cy0, 0), cx1 = std::min(cx1, int(std::lround(W * S))), cy1 = std::min(cy1, int(std::lround(H * S)));
    const int bw = cx1 - cx0, bh = cy1 - cy0;
    if (bw <= 0 || bh <= 0)
        return;
    std::vector<float> cov(size_t(bw) * size_t(bh), 0.0f), hgt(cov.size(), 0.0f), gls(cov.size(), 0.0f), ton(cov.size(), 0.0f);
    auto put = [&](int px, int py, float c, float h, float g, float t) {
        const size_t i = size_t(py - cy0) * size_t(bw) + size_t(px - cx0);
        cov[i] = std::max(cov[i], c);
        if (h > hgt[i]) hgt[i] = h, ton[i] = t;
        gls[i] = std::max(gls[i], g * clamp01(c));
    };
    // a shape over its box (units): fn(ux, uy, f, h) gives the signed distance inside (units) and the height 0..1
    auto shape = [&](float x0, float y0, float x1, float y1, float gloss, float tone, auto&& fn) {
        const int X0 = std::max(cx0, int(x0 * S) - 1), X1 = std::min(cx1, int(x1 * S) + 2);
        const int Y0 = std::max(cy0, int(y0 * S) - 1), Y1 = std::min(cy1, int(y1 * S) + 2);
        for (int py = Y0; py < Y1; ++py)
            for (int px = X0; px < X1; ++px) {
                float f = -1, h = 0;
                fn((float(px) + 0.5f) / S, (float(py) + 0.5f) / S, f, h);
                const float c = clamp01(f * S + 0.5f);
                if (c > 0) put(px, py, c, h, gloss, tone);
            }
    };
    auto dome = [](float f, float r) { const float q = clamp01(f / std::max(r, 0.01f)); return 1.0f - (1.0f - q) * (1.0f - q); };
    auto disc = [&](float cx, float cy, float r, float hs, float gloss, float tone) {
        shape(cx - r, cy - r, cx + r, cy + r, gloss, tone, [&](float ux, float uy, float& f, float& h) {
            f = r - std::hypot(ux - cx, uy - cy);
            h = hs * dome(f, r * 0.35f);
        });
    };
    // a drop flying along (dx, dy): a teardrop longer the further it flew, its fat end ahead
    auto drop = [&](float cx, float cy, float dx, float dy, float r, float el, float tone) {
        const float L = r * el;
        shape(cx - L - r, cy - L - r, cx + L + r, cy + L + r, 0.0f, tone, [&](float ux, float uy, float& f, float& h) {
            const float u = (ux - cx) * dx + (uy - cy) * dy, v = -(ux - cx) * dy + (uy - cy) * dx;
            const float t = std::clamp(u / L, -1.0f, 1.0f);
            const float wdt = r * std::sqrt(std::max(0.0f, 1.0f - t * t)) * (0.45f + 0.55f * (t + 1) / 2);
            f = std::min(wdt - std::fabs(v), (L - std::fabs(u)) * 0.6f);
            h = std::min(0.85f, 0.3f + r * 0.35f) * dome(f, std::max(wdt, 0.3f));
        });
    };
    // a run down from (x, y): len long, w wide, thinning to a third, a bead at its end
    auto run = [&](float x, float y, float len, float w, int seed, float tone) {
        auto cxAt = [&](float uy) { return x + (vnoise((uy - y) * 0.04f, float(seed), 711) - 0.5f) * 3.0f + (uy - y) * 0.004f * float(seed % 3 - 1); };
        shape(x - w - 4, y, x + w + 4, y + len, 0.6f, tone, [&](float ux, float uy, float& f, float& h) {
            const float t = (uy - y) / len, ww = w * (1.0f - 0.68f * std::pow(t, 0.7f));
            f = std::min(ww - std::fabs(ux - cxAt(uy)), (uy - y) + 1.0f);
            h = (0.55f + 0.3f * (1 - t)) * dome(f, ww);
        });
        const float br = w * 0.62f + 0.35f, ey = y + len + br * 0.4f;
        disc(cxAt(y + len), ey, br, 0.8f, 0.7f, tone);
    };
    // a blob with lobes and noisy rim (r units), its spray thrown along ang (+- spread) and runs down from it
    auto blob = [&](float cx, float cy, float r, float ang, float spread, int seed, int runs) {
        const float tone = rnd(seed, 1, 720);
        shape(cx - r * 1.5f, cy - r * 1.5f, cx + r * 1.5f, cy + r * 1.5f, r >= 5 ? 1.0f : 0.4f, tone,
              [&](float ux, float uy, float& f, float& h) {
            const float a = std::atan2(uy - cy, ux - cx);
            const float rr = r * (0.62f + 0.62f * fbm(std::cos(a) * 1.4f + 7.0f, std::sin(a) * 1.4f + float(seed), 721));
            f = rr - std::hypot(ux - cx, uy - cy);
            h = dome(f, r * 0.28f);
        });
        const int lobes = 3 + int(rnd(seed, 2, 720) * 5);
        for (int k = 0; k < lobes; ++k) {
            const float a = rnd(seed, 10 + k, 722) * 6.2832f, d = r * (0.6f + rnd(seed, 30 + k, 722) * 0.45f);
            disc(cx + std::cos(a) * d, cy + std::sin(a) * d, r * (0.18f + rnd(seed, 50 + k, 722) * 0.25f), 0.8f, 0.8f, tone);
        }
        const int n = int(r * 3.0f) + 6;
        for (int k = 0; k < n; ++k) {   // the spray: mostly along ang, further = smaller and longer
            const float a = ang + (rnd(seed, 100 + k, 723) - 0.5f) * 2.0f * spread;
            const float q = rnd(seed, 300 + k, 723), d = r * (1.05f + std::pow(q, 1.6f) * 4.5f);
            const float dr = (0.35f + std::pow(rnd(seed, 500 + k, 723), 2.0f) * r * 0.26f) * (1.15f - q * 0.6f);
            drop(cx + std::cos(a) * d, cy + std::sin(a) * d, std::cos(a), std::sin(a), dr, 1.0f + q * 1.7f, tone);
        }
        for (int k = 0; k < runs; ++k) {
            const float rx = cx + (rnd(seed, 700 + k, 724) - 0.5f) * r * 1.1f;
            run(rx, cy + r * 0.3f, 15.0f + std::pow(rnd(seed, 720 + k, 724), 1.6f) * 110.0f,
                1.3f + rnd(seed, 740 + k, 724) * std::min(2.4f, r * 0.15f), seed * 7 + k, tone);
        }
    };

    // 1. the stains: smeared, mottled, in the top corners and thin down the sides
    {
        const int X0 = cx0, X1 = cx1, Y0 = cy0, Y1 = std::min(cy1, int(H * 0.8f * S));
        for (int py = Y0; py < Y1; ++py)
            for (int px = X0; px < X1; ++px) {
                const float ux = (float(px) + 0.5f) / S, uy = (float(py) + 0.5f) / S;
                const float dl = std::hypot(ux / (W * 0.26f), uy / (H * 0.5f)), dr = std::hypot((W - ux) / (W * 0.24f), uy / (H * 0.46f));
                const float side = std::max(1.0f - std::min(ux, W - ux) / (W * 0.045f), 0.0f) * clamp01(1.0f - uy / (H * 0.8f));
                const float base = std::max({1.0f - dl, 1.0f - dr, side * 0.8f});
                if (base <= -0.3f)
                    continue;
                const float m = base + (fbm(ux * 0.025f, uy * 0.025f, 731 + so) - 0.5f) * 0.9f + (vnoise(ux * 0.16f, uy * 0.16f, 732 + so) - 0.5f) * 0.22f;
                const float c = clamp01((m - 0.26f) / 0.14f) * 0.78f;
                if (c > 0)
                    put(px, py, c, 0.38f + 0.5f * clamp01((m - 0.26f) / 0.5f), 0.0f, clamp01(m));
            }
    }
    // 2. the blobs (placed per width: in the corners' stains, down the sides, a few spots below)
    struct B { float x, y, r, ang, spread; int runs; };
    const B blobs[] = {
        {W * 0.035f, H * 0.06f, 17, 0.75f, 0.7f, 2}, {W * 0.13f, H * 0.035f, 11, 1.1f, 0.6f, 3}, {W * 0.21f, H * 0.11f, 6, 0.9f, 0.5f, 1},
        {W * 0.06f, H * 0.27f, 9, 0.4f, 0.6f, 1},    {W * 0.025f, H * 0.52f, 7, 0.1f, 0.5f, 1}, {W * 0.09f, H * 0.66f, 4, 0.3f, 0.6f, 0},
        {W * 0.96f, H * 0.05f, 16, 2.4f, 0.7f, 2},   {W * 0.86f, H * 0.03f, 10, 2.0f, 0.6f, 2}, {W * 0.8f, H * 0.12f, 5, 2.3f, 0.5f, 1},
        {W * 0.94f, H * 0.3f, 10, 2.9f, 0.6f, 1},    {W * 0.975f, H * 0.58f, 6, 3.0f, 0.5f, 1}, {W * 0.9f, H * 0.8f, 5, 3.6f, 0.8f, 0},
        {W * 0.31f, H * 0.012f, 6, 1.5f, 0.6f, 2},   {W * 0.69f, H * 0.01f, 5, 1.6f, 0.6f, 1},  {W * 0.04f, H * 0.88f, 5, -0.5f, 0.7f, 0},
    };
    int seed = 1 + so * 16;
    for (const B& b : blobs) {
        const int k = seed;
        const float x = b.x + (rnd(k, 1, 761) - 0.5f) * W * 0.05f * float(jit), y = std::max(0.0f, b.y + (rnd(k, 2, 761) - 0.5f) * H * 0.07f * float(jit));
        const float r = b.r * (1.0f + (rnd(k, 3, 761) - 0.5f) * 0.45f * float(jit)), ang = b.ang + (rnd(k, 4, 761) - 0.5f) * 0.7f * float(jit);
        const int runs = std::max(0, b.runs + (jit ? int(rnd(k, 5, 761) * 3.0f) - 1 : 0));
        blob(x, y, r, ang, b.spread, seed++, runs);
    }
    // 3. the fine spatter: from the top corners and the sides, densest near them
    for (int k = 0; k < 520; ++k) {
        const float z = rnd(k, 1, 741 + so), q = std::pow(rnd(k, 2, 741 + so), 1.7f);
        float sx, sy, a;
        if (z < 0.6f) {   // a top corner
            const bool left = z < 0.31f;
            a = (0.05f + rnd(k, 3, 741 + so) * 1.45f);
            sx = left ? 0.0f : W, sy = 0;
            const float d = q * H * 0.62f;
            if (!left) a = 3.1416f - a;
            sx += std::cos(a) * d * 1.3f, sy += std::sin(a) * d;
        } else if (z < 0.9f) {   // a side, thrown inwards
            const bool left = z < 0.76f;
            sy = rnd(k, 4, 741 + so) * H * 0.9f;
            a = (rnd(k, 5, 741 + so) - 0.5f) * 1.3f + (left ? 0.0f : 3.1416f);
            const float d = q * W * 0.16f;
            sx = (left ? 0.0f : W) + std::cos(a) * d, sy += std::sin(a) * d;
        } else {   // a bottom corner, sparse
            const bool left = z < 0.94f;
            a = -(0.1f + rnd(k, 6, 741 + so) * 1.3f);
            if (!left) a = -3.1416f - a;
            const float d = q * H * 0.4f;
            sx = (left ? 0.0f : W) + std::cos(a) * d * 1.2f, sy = H + std::sin(a) * d;
        }
        const float r = (0.32f + std::pow(rnd(k, 7, 741 + so), 3.0f) * 2.2f) * (1.2f - q * 0.7f);
        drop(sx, sy, std::cos(a), std::sin(a), r, 1.0f + q * 1.5f * rnd(k, 8, 741 + so), rnd(k, 9, 741 + so));
    }
    // 4. runs from the top edge (out of the corners' stains)
    for (int k = 0; k < 6; ++k) {
        const bool left = k % 2 == 0;
        const float x = left ? W * (0.01f + rnd(k, 1, 751 + so) * 0.24f) : W * (0.99f - rnd(k, 1, 751 + so) * 0.22f);
        run(x, 0.0f, 30.0f + std::pow(rnd(k, 2, 751 + so), 1.4f) * 130.0f, 1.2f + rnd(k, 3, 751 + so) * 1.8f, 900 + k + so, rnd(k, 4, 751 + so));
    }

    // the shading: thin = lighter, see-through; thick = dark to near black; the wet highlight from the height's slope
    for (int py = cy0; py < cy1; ++py)
        for (int px = cx0; px < cx1; ++px) {
            const size_t i = size_t(py - cy0) * size_t(bw) + size_t(px - cx0);
            const float c = cov[i];
            if (c <= 0.004f)
                continue;
            const float h = hgt[i], tn = ton[i];
            uint32_t col = mix(0xA0180E, 0x5A0604, clamp01(h / 0.4f));
            col = mix(col, mix(0x2E0302, 0x140101, tn), clamp01((h - 0.35f) / 0.6f));
            if (gls[i] > 0.01f) {
                auto at = [&](int x, int y) {
                    x = std::clamp(x, cx0, cx1 - 1), y = std::clamp(y, cy0, cy1 - 1);
                    return hgt[size_t(y - cy0) * size_t(bw) + size_t(x - cx0)];
                };
                const float k = 2.2f * S;   // (the slope per unit)
                const float gx = (at(px + 1, py) - at(px - 1, py)) * 0.5f * k, gy = (at(px, py + 1) - at(px, py - 1)) * 0.5f * k;
                const float nl = 1.0f / std::sqrt(gx * gx + gy * gy + 1.0f);
                // the half vector between the light (upper left) and the eye
                const float hx = -0.27f, hy = -0.42f, hz = 0.866f;
                const float nh = std::max(0.0f, (-gx * hx - gy * hy + hz) * nl);
                const float sp = std::pow(nh, 48.0f) * gls[i];
                col = mix(col, 0xF0B0A0, sp * 0.7f);
            }
            out(px, py, tintHue(J.hue, col), c * (0.42f + 0.55f * clamp01(h / 0.35f)));
        }
}
} // namespace

void Frontend::rSplatter(int cx0, int cy0, int cx1, int cy1)
{
    SplatJob j;
    j.cw = cw_, j.ps = ps_, j.seed = splatSeed, j.hue = bloodHue_;
    splatterDraw(j, cx0, cy0, cx1, cy1, [&](int x, int y, uint32_t c, float a) { rBlend(x, y, c, a); });
}

// blood2: the GPU menu's splatter layer, whole (rpw x rph RGBA, straight alpha): the left, the right and the top middle
// as drawRPanel's tiles, each pixel written once (the parts do not overlap)
std::vector<uint8_t> Frontend::splatterLayer(int cw, float ps, uint32_t seed, uint32_t hue)
{
    SplatJob j;
    j.cw = cw, j.ps = ps, j.seed = seed, j.hue = hue;
    const int PW = int(std::lround(float(cw) * ps)), PH = int(std::lround(float(kHeight) * ps));
    std::vector<uint8_t> img(size_t(PW) * size_t(PH) * 4, 0);
    auto put = [&](int x, int y, uint32_t c, float a) {
        if (x < 0 || y < 0 || x >= PW || y >= PH || a <= 0)
            return;
        uint8_t* d = &img[(size_t(y) * size_t(PW) + size_t(x)) * 4];
        d[0] = uint8_t(c >> 16), d[1] = uint8_t(c >> 8), d[2] = uint8_t(c), d[3] = uint8_t(std::min(a, 1.0f) * 255.0f + 0.5f);
    };
    const int bx = int(float(cw) * 0.36f), ex = cw - bx, th = int(kHeight * 0.3f);
    auto px = [&](int u) { return int(float(u) * ps); };
    splatterDraw(j, 0, 0, px(bx), PH, put);
    splatterDraw(j, px(ex), 0, PW, PH, put);
    splatterDraw(j, px(bx), 0, px(ex), px(th), put);
    return img;
}

// ---- the widgets' draws ----
void Frontend::drawRPanel(Widget& w)
{
    const int* p = rPanel_;
    // the splatter (rSplatter; per width): the left, the right and the top middle, each a tile (the GPU menu: its own
    // layer under this one, Frontend::splatterLayer)
    if (!gpuLayers) {
        const int bx = int(float(cw_) * 0.36f), ex = cw_ - bx, th = int(kHeight * 0.3f);
        auto px = [&](int u) { return int(float(u) * ps_); };
        rCached("SL" + std::to_string(cw_), 0.0f, 0.0f, float(bx), float(kHeight), 0.0f, [&]() { rSplatter(0, 0, px(bx), rph()); });
        rCached("SR" + std::to_string(cw_), float(ex), 0.0f, float(cw_ - ex), float(kHeight), 0.0f,
                [&]() { rSplatter(px(ex), 0, rpw(), rph()); });
        rCached("SM" + std::to_string(cw_), float(bx), 0.0f, float(ex - bx), float(th), 0.0f, [&]() { rSplatter(px(bx), 0, px(ex), px(th)); });
    }
    // under the panel's red bottom: the arcade's smear (0x2D) laid on its side, peeking out below the glow
    if (w.user != 2) {
        char key[64];
        std::snprintf(key, sizeof key, "B%d,%d,%d,%d", p[0], p[1], p[2], p[3]);
        const float bx = float(p[0]) + float(p[2]) / 2, by = float(p[1] + p[3]) - 2.0f;
        rCached(key, float(p[0]), by - 30.0f, float(p[2]), 60.0f, 0.0f, [&]() {
            loadBloodArt();
            const BloodArt& b = bloodArt_[5];
            if (b.w <= 0)
                return;
            const float len = float(p[2]) * 0.5f, thick = 30.0f;   // (its length across, its thickness down)
            const float sx = thick / (float(b.w) * len / float(b.h));
            rBlood(5, bx - float(p[2]) * 0.2f, by, len, 1.5708f, 0.85f, false, sx);
            rBlood(5, bx + float(p[2]) * 0.22f, by + 1.0f, len * 0.9f, -1.5708f, 0.75f, true, sx);
        });
    }
    if (w.user == 0) {   // the main menu: the logo over the panel's top
        rTorn(float(p[0]), float(p[1]), float(p[2]), float(p[3]), 3, true);
        if (logoW_ > 0) {
            const float lw = std::min(340.0f, float(cw_) * 0.5f), lh = lw * float(logoH_) / float(logoW_);
            const float lx = (float(cw_) - lw) / 2, ly = std::max(4.0f, float(p[1]) - lh + 22.0f);
            rImage(logo_, logoW_, logoH_, lx, ly, lw, lh, 1.0f);
            if (wordW_ > 0) {   // logo2: the wordmark centred at the logo's (286, 282) (of its 645 x 340), 3x art
                const float k = lw / float(logoW_), ww = float(wordW_) / 3 * k, wh = float(wordH_) / 3 * k;
                rImageSoft(word_, wordW_, wordH_, lx + 286.0f * k - ww / 2, ly + 282.0f * k - wh / 2, ww, wh);
            }
        }
    } else
        rTorn(float(p[0]), float(p[1]), float(p[2]), float(p[3]), w.user == 1 ? 9 : 5, true);
}

void Frontend::drawRStrip(Widget& w)
{
    int x, y, ww, hh;
    absRect(w, x, y, ww, hh);
    const bool lit = (w.flags & 0x40000) != 0;
    if (w.user == 2) {   // a group's heading: red capitals on the panel, no strip
        rText(w.text, float(x + ww / 2), float(y + 2), 19.0f, tint(kInkLit), 1, 0.12f, w.seed, kTxOutline | kTxShadow);
        return;
    }
    const float tilt = (float(w.seed % 3) - 1.0f) * 0.006f;
    // playerpick: user 3 / 4: a big number strip (4: the chosen one: the red glow, the light paper, red letters; the
    // focus on a number: a little larger, red letters, the claw marks)
    const bool big = w.user == 3 || w.user == 4, chosen = w.user == 4;
    const int sx = lit && !big ? 5 : 0;   // the strip under the hand slides a little to the right
    const int gx = lit ? (big ? 4 : 5) : 0, gy = lit ? (big ? 3 : 2) : 0;   // (lit: a little larger, in a red glow)
    if (big ? chosen : lit) {
        const float S = ps_, R = big ? 11.0f : 9.0f, ga = big ? 0.8f : 0.55f;
        const float x0 = float(x + sx - gx), y0 = float(y - gy), x1 = x0 + float(ww + 2 * gx), y1 = y0 + float(hh + 2 * gy);
        for (int py = std::max(0, int((y0 - R) * S)); py < int((y1 + R) * S); ++py)
            for (int px = std::max(0, int((x0 - R) * S)); px < int((x1 + R) * S); ++px) {
                const float ux = float(px) / S, uy = float(py) / S;
                const float dx = std::max({x0 - ux, ux - x1, 0.0f}), dy = std::max({y0 - uy, uy - y1, 0.0f});
                const float d = std::sqrt(dx * dx + dy * dy);
                if (d < R)
                    rBlend(px, py, tint(0xB01008), ga * (1.0f - d / R) * (1.0f - d / R));
            }
    }
    rPaper(float(x + sx - gx), float(y - gy), float(ww + 2 * gx), float(hh + 2 * gy), w.seed, tilt,
           lit || chosen ? 0 : w.user == 1 ? 2 : 1);
    float th = big ? std::min(26.0f, float(hh) * 0.62f) : std::min(15.0f, float(hh) * 0.6f);
    // polish (user 2026-11-03: the More Modes labels ran past their strips): a label too long for the paper (inside its
    // torn edges) smaller; a tag (Survival's "Very Experimental / Proof of Concept") a small red line under it
    const float room = float(ww) - 30.0f;
    float tagH = 0.0f;
    if (!big) {
        if (!w.value.empty()) {
            th = std::min(th, float(hh) * 0.5f);
            tagH = std::min(th * 0.7f, float(hh) * 0.34f);
            if (const float tw = rTextWidth(w.value, tagH); tw > room) tagH = std::max(6.0f, tagH * room / tw);
        }
        if (const float tw = rTextWidth(w.text, th); tw > room) th = std::max(8.0f, th * room / tw);
        if (langCheck && rTextWidth(w.text, th) > room + 2.0f)   // (the sweep: still wider than the strip)
            langOverflow(langOn() ? lang::tr(w.text) : w.text, 0, int(rTextWidth(w.text, th)), int(room), &w);
    }
    const float gap = tagH > 0.0f ? 1.0f : 0.0f, ty = float(y) + (float(hh) - th - gap - tagH) / 2 + 1;
    rText(w.text, float(x + sx + ww / 2), ty, th, lit || chosen ? paperInk(kInkLit) : w.user == 1 ? kInkGrey : paperInk(kInk), 1,
          0.12f, w.seed, w.user == 1 ? kTxShadow : kTxOutline | kTxShadow);
    if (tagH > 0.0f)
        rText(w.value, float(x + sx + ww / 2), ty + th + gap, tagH, w.user == 1 ? kInkGrey : paperInk(0xA01810), 1, 0.0f, w.seed + 1, 0);
    if (lit)   // the arcade's claw marks (0x2A) at the strip's left end
        rBlood(2, float(x + sx - (big ? 6 : 12)), float(y + hh / 2), float(hh) * (big ? 1.1f : 1.5f), 0.0f, 0.9f);
}

void Frontend::drawRTab(Widget& w)
{
    int x, y, ww, hh;
    absRect(w, x, y, ww, hh);
    const bool cur = w.user == 1, lit = (w.flags & 0x40000) != 0;
    rPaper(float(x + 2), float(y), float(ww - 4), float(hh), w.seed, 0.0f, cur ? 0 : 1);
    float th = cur ? 17.0f : 14.0f;
    // (a narrow screen's many tabs: fitted inside the paper's torn edges; finalqa: 3:2's "Controls" touched them at -14)
    if (const float tw = rTextWidth(w.text, th); tw > float(ww - 24))
        th *= float(ww - 24) / tw;
    // the current / hovered tab: red with the outline and shadow (as the strips); the others dark ink, a faint shadow
    rText(w.text, float(x + ww / 2), float(y) + (float(hh) - th) / 2, th, cur ? paperInk(kInkLit) : lit ? paperInk(0xB01810) : kTabText, 1, 0.08f,
          w.seed, cur || lit ? kTxOutline | kTxShadow : 0);
}

void Frontend::drawRRow(Widget& w)
{
    int x, y, ww, hh;
    absRect(w, x, y, ww, hh);
    const bool lit = (w.flags & 0x40000) != 0, grey = w.user == 1;
    const float S = ps_;
    // rtl (a right-to-left language): the row mirrored: the label at the right, the value / switch / mark at the left,
    // the focus bar fading to the left with its mark at the right. mx: a position in the row mirrored; al: an alignment
    const bool rtl = lang::rtl();
    auto mx = [&](float a) { return rtl ? float(2 * x + ww) - a : a; };
    auto al = [&](int a) { return rtl && a != 1 ? 2 - a : a; };
    if (lit) {   // the focus: a red bar fading to the right, a mark at its left
        for (int py = int(float(y) * S); py < int(float(y + hh) * S); ++py)
            for (int px = int(float(rtl ? x : x - 8) * S); px < int(float(rtl ? x + ww + 8 : x + ww) * S); ++px) {
                const float t = rtl ? (float(x + ww + 8) - float(px) / S) / float(ww + 8) : (float(px) / S - float(x - 8)) / float(ww + 8);
                rBlend(px, py, tint(0x7A0A08), 0.42f * (1.0f - t * 0.85f) * (0.8f + 0.2f * vnoise(float(px) * 0.05f, float(py) * 0.2f, 3)));
            }
        for (int py = int(float(y + 2) * S); py < int(float(y + hh - 2) * S); ++py)
            for (int px = int(float(rtl ? x + ww + 5 : x - 8) * S); px < int(float(rtl ? x + ww + 8 : x - 5) * S); ++px)
                rBlend(px, py, tint(0xD0180E), 0.95f);
    }
    const float th = 15.0f, ty = float(y) + (float(hh) - th) / 2;   // (blood2: was 11)
    const uint32_t col = grey ? kRowOff : lit ? kRowLit : kRowText;
    const int fx = grey ? kTxShadow : kTxOutline | kTxShadow;
    rText(w.text, mx(float(x + 6)), ty, th, col, al(0), 0.06f, w.seed + 1, fx);
    float right = float(x + ww - 6);
    if (w.seed == 1) {   // experimental: a small red tag after the label
        const float lw = rTextWidth(w.text, th);
        rText("Experimental", mx(float(x + 6) + lw + 10), ty + 4, 9.0f, tint(0xD03022), al(0), 0.0f, 3, kTxOutline);
    }
    std::string v = w.value;
    if (v.empty())
        return;
    if (v == "On" || v == "Off") {   // a switch: the handprint
        rHand(mx(right - 9), float(y + hh / 2), 18.0f, v == "On");
        return;
    }
    const bool open = v.size() > 1 && v.back() == '>';   // a sub-page ("Open >"): a right-pointing mark
    if (open) {
        v.pop_back();
        while (!v.empty() && v.back() == ' ') v.pop_back();
    }
    if (!grey) {
        if (open)
            rText(rtl ? "<" : ">", mx(right - 6), ty, th, col, 1, 0.0f, 0, fx);
        else {
            rChevron(mx(right - 5.4f), float(y + hh / 2) + 1.0f, 6.0f, 0x000000, 0.6f);   // (its shadow)
            rChevron(mx(right - 6), float(y + hh / 2), 6.0f, col);
        }
    }
    right -= 18;
    // the value fitted beside the label (cut with "..." when it would run into it)
    const float avail = right - (float(x + 6) + rTextWidth(w.text, th) + 24);
    // (misc17: a whole letter at a time (UTF-8), and it ends: before, when not even "..." fitted beside a long label
    // (Hebrew, Japanese), "..." / "....." / "...." went round for ever)
    // (soak2: translated before it is cut: a cut English text has no translation and was drawn in English)
    if (langOn())
        v = lang::tr(v);
    if (v.size() > 1)
        v = util::utf8Fit(v, "...", [&](const std::string& t) { return rTextWidthRaw(t, th) <= avail; });   // (util/utf8.h)
    const RawText raw(this);
    rText(v, mx(right), ty, th, col, al(2), 0.06f, w.seed + 2, fx);
}

void Frontend::drawRPrompt(Widget& w)
{
    int x, y, ww, hh;
    absRect(w, x, y, ww, hh);
    const bool lit = (w.flags & 0x40000) != 0;
    const size_t bar = w.value.find('|');
    const bool gun = gunPrompts && (w.value == "Reload|Reload" || w.value == "Shoot|Shoot");   // (Light Gun Edition)
    const std::string key = gun ? w.value.substr(0, bar) : padPrompts_ ? padKey(w.value.substr(bar + 1)) : w.value.substr(0, bar);
    // a column at the widget's right: the key caps all one size (keys a rounded box, a pad's buttons a ring) centred
    // on one line, the arcade's claw marks (0x2A) small behind each, the label right-aligned a fixed gap to their left
    const float kh = kPromptCapH, colW = kPromptCapW;
    // (leftovers, rtl: mirrored: the caps at the widget's left, the label left-aligned a gap to their right)
    const float kw = padPrompts_ && !gun ? kh : colW, ccx = w.rtlRow ? float(x) + colW / 2 : float(x + ww) - colW / 2;
    const float kx = ccx - kw / 2, ky = float(y) + (float(hh) - kh) / 2;
    rBlood(2, ccx + 4.0f, ky + kh / 2 - 2.0f, 40.0f, 0.38f, 0.72f);   // (out past the cap's top right / bottom left)
    const float th = 7.0f;
    if (gun || !rPadGlyph(key, ccx, ky + kh / 2, kh / 2)) {   // (padglyphs: the pad's / keys' icons, else the cap)
        rKeyCap(kx, ky, kw, kh, padPrompts_ && !gun);
        if (gun || !rPadSymbol(key, ccx, ky + kh / 2, kh / 2))   // (padpass: a PlayStation pad's shapes)
            rText(key, ccx, ky + (kh - th) / 2, th, 0xF4ECE4, 1, 0.0f, 0);
    }
    const float lh = 12.0f;
    rText(w.text, w.rtlRow ? float(x) + colW + kPromptGap : float(x + ww) - colW - kPromptGap, ky + (kh - lh) / 2, lh,
          lit && w.activate ? tint(kInkLit) : 0xE2DAD2, w.rtlRow ? 0 : 2, 0.06f, w.seed, kTxOutline | kTxShadow);
}

void Frontend::drawRLabel(Widget& w)
{
    int x, y, ww, hh;
    absRect(w, x, y, ww, hh);
    float h = float(hh) * 0.66f, dy = 0;
    if (w.fitW > 0) {   // namefit: a long name ("Researcher (Woman, 4)") smaller to fit (between the arrows), centred
        const float tw = rTextWidth(w.text, h);
        if (tw > float(w.fitW)) {
            const float h2 = std::max(h * 0.55f, h * float(w.fitW) / tw);
            dy = (h - h2) * 0.5f, h = h2;
        }
    }
    rText(w.text, float(x + ww / 2), float(y) + 4 + dy, h, uint32_t(w.user), 1, 0.1f, w.seed, kTxOutline | kTxShadow);
}

void Frontend::drawRArrow(Widget& w)
{
    int x, y, ww, hh;
    absRect(w, x, y, ww, hh);
    const bool lit = (w.flags & 0x40000) != 0;
    rChevron(float(x + ww / 2), float(y + hh / 2), w.user ? 6.0f : -6.0f, lit ? tint(kInkLit) : kRowText);
}

// remakefix: a dropdown's panel: a soft shadow, a small dark torn panel (no glow), a thin red line at its top
void Frontend::drawRDrop(Widget& w)
{
    int x, y, ww, hh;
    absRect(w, x, y, ww, hh);
    const float S = ps_;
    for (int py = int(float(y + 2) * S); py < int(float(y + hh + 6) * S); ++py)   // (its shadow, down and right)
        for (int px = int(float(x + 2) * S); px < int(float(x + ww + 6) * S); ++px) {
            const float dx = std::max(0.0f, float(px) / S - float(x + ww)), dy = std::max(0.0f, float(py) / S - float(y + hh));
            rBlend(px, py, 0x000000, 0.45f * (1.0f - std::min(1.0f, std::sqrt(dx * dx + dy * dy) / 6.0f)));
        }
    // (an opaque base first: the rows under it never show through the torn panel's haze)
    for (int py = int(float(y + 3) * S); py < int(float(y + hh - 3) * S); ++py)
        for (int px = int(float(x + 3) * S); px < int(float(x + ww - 3) * S); ++px)
            rBlend(px, py, 0x15100F, 1.0f);
    rTorn(float(x), float(y), float(ww), float(hh), w.seed, false, 0.97f);
    for (int px = int(float(x + 6) * S); px < int(float(x + ww - 6) * S); ++px)
        for (int py = int(float(y + 1) * S); py < int(float(y + 2) * S); ++py)
            rBlend(px, py, tint(0xA00C08), 0.8f * (0.7f + 0.3f * vnoise(float(px) * 0.1f, 0.0f, w.seed)));
}

// remakefix: a dropdown's value: the focus as a row's (a red bar fading right, a mark at its left), the current one in
// red with a small red diamond before it
void Frontend::drawRDropItem(Widget& w)
{
    int x, y, ww, hh;
    absRect(w, x, y, ww, hh);
    const bool lit = (w.flags & 0x40000) != 0, cur = (w.user & 1) != 0, off = (w.user & 2) != 0;
    const float S = ps_;
    if (lit) {
        for (int py = int(float(y + 1) * S); py < int(float(y + hh - 1) * S); ++py)
            for (int px = int(float(x) * S); px < int(float(x + ww) * S); ++px) {
                const float t = (float(px) / S - float(x)) / float(ww);
                rBlend(px, py, tint(0x7A0A08), 0.5f * (1.0f - t * 0.8f) * (0.8f + 0.2f * vnoise(float(px) * 0.05f, float(py) * 0.2f, 3)));
            }
        for (int py = int(float(y + 3) * S); py < int(float(y + hh - 3) * S); ++py)
            for (int px = int(float(x) * S); px < int(float(x + 3) * S); ++px)
                rBlend(px, py, tint(0xD0180E), 0.95f);
    }
    const float th = 13.0f, ty = float(y) + (float(hh) - th) / 2;
    if (cur) {   // (the current value: a small diamond)
        const float cx = float(x + 13), cy = float(y + hh / 2), r = 3.2f;
        for (int py = int((cy - r) * S); py <= int((cy + r) * S); ++py)
            for (int px = int((cx - r) * S); px <= int((cx + r) * S); ++px) {
                const float d = std::fabs(float(px) / S - cx) + std::fabs(float(py) / S - cy);
                rBlend(px, py, tint(0xD0180E), std::clamp((r - d) * S * 0.7f, 0.0f, 1.0f));
            }
    }
    float tx = float(x + 24);
    if (ddRows_ && ddRow_ >= 0 && ddRow_ < int(ddRows_->size()) && (*ddRows_)[size_t(ddRow_)].art) {   // optui: its icon
        if (const SightArt* a = rowArt(ddRow_, w.id, artSeat, rph()); a && a->w > 0)
            rSightArt(*a, float(x + 38), float(y) + float(hh) / 2, float(hh - 4));
        tx = float(x + 56);
    }
    rText(w.text, tx, ty, th, off ? kRowOff : cur ? tint(kInkLit) : lit ? kRowLit : kRowText, 0, 0.04f, w.seed, kTxOutline | kTxShadow);
}

// ---- optui: the crosshair preview (user 2026-10-26: "show the crosshair options so players know what it looks like
// before playing") ----

// a row's sight picture (game/sights.h SightArt), kept per settings / choice / seat / window height
const SightArt* Frontend::rowArt(int row, int choice, int seat, int wh)
{
    if (!artRows_ || row < 0 || row >= int(artRows_->size()) || !(*artRows_)[size_t(row)].art)
        return nullptr;
    const std::string key = artSig_ + "#" + std::to_string(row) + "#" + std::to_string(choice) + "#" + std::to_string(seat) + "#" +
                            std::to_string(wh);
    auto it = artCache_.find(key);
    if (it == artCache_.end()) {
        if (artCache_.size() > 256)
            artCache_.clear();
        SightArt a;
        if (!(*artRows_)[size_t(row)].art(choice, seat, wh, a))
            a = SightArt{};
        it = artCache_.emplace(key, std::move(a)).first;
    }
    return &it->second;
}

namespace {
// a sight picture's pixel at (u, v) of its drawn size (bilinear when smooth; straight alpha out)
void artSample(const SightArt& a, float u, float v, bool smooth, float& r, float& g, float& b, float& al)
{
    if (!smooth) {
        const int sx = std::clamp(int(u), 0, a.w - 1), sy = std::clamp(int(v), 0, a.h - 1);
        const uint8_t* p = &a.rgba[(size_t(sy) * size_t(a.w) + size_t(sx)) * 4];
        r = p[0], g = p[1], b = p[2], al = p[3] / 255.0f;
        return;
    }
    const float fx = std::clamp(u - 0.5f, 0.0f, float(a.w - 1)), fy = std::clamp(v - 0.5f, 0.0f, float(a.h - 1));
    const int x0 = int(fx), y0 = int(fy), x1 = std::min(x0 + 1, a.w - 1), y1 = std::min(y0 + 1, a.h - 1);
    const float ax = fx - float(x0), ay = fy - float(y0);
    const float wt[4] = {(1 - ax) * (1 - ay), ax * (1 - ay), (1 - ax) * ay, ax * ay};
    const int xs[4] = {x0, x1, x0, x1}, ys[4] = {y0, y0, y1, y1};
    r = g = b = al = 0;
    for (int k = 0; k < 4; ++k) {
        const uint8_t* p = &a.rgba[(size_t(ys[k]) * size_t(a.w) + size_t(xs[k])) * 4];
        const float pa = p[3] / 255.0f * wt[k];
        r += p[0] * pa, g += p[1] * pa, b += p[2] * pa, al += pa;
    }
    if (al > 0)
        r /= al, g /= al, b /= al;
}
}

// a sight at (cx, cy) units, its pixels the canvas's (as in the game at this window height); fit: at most that many units
void Frontend::rSightArt(const SightArt& a, float cx, float cy, float fit)
{
    if (a.w <= 0 || a.h <= 0 || a.rgba.size() < size_t(a.w) * size_t(a.h) * 4)
        return;
    const float S = ps_;
    float k = 1.0f;
    if (fit > 0 && float(std::max(a.dw, a.dh)) > fit * S)
        k = fit * S / float(std::max(a.dw, a.dh));
    const int DW = std::max(1, int(std::lround(float(a.dw) * k))), DH = std::max(1, int(std::lround(float(a.dh) * k)));
    const int X0 = int(std::floor(cx * S)) - int(std::lround(float(a.cx) * k)), Y0 = int(std::floor(cy * S)) - int(std::lround(float(a.cy) * k));
    const bool smooth = a.linear || k != 1.0f || a.w != a.dw || a.h != a.dh;
    for (int j = 0; j < DH; ++j)
        for (int i = 0; i < DW; ++i) {
            float r, g, b, al;
            artSample(a, (float(i) + 0.5f) * float(a.w) / float(DW), (float(j) + 0.5f) * float(a.h) / float(DH), smooth, r, g, b, al);
            if (al > 0.003f)
                rBlend(X0 + i, Y0 + j, (uint32_t(r + 0.5f) << 16) | (uint32_t(g + 0.5f) << 8) | uint32_t(b + 0.5f), al);
        }
}

// the sample game picture: the live attract's saved still (previewStills), else the hall (0xD1) in its own colours
void Frontend::loadPreviewBg()
{
    if (prevBgTried_)
        return;
    prevBgTried_ = true;
    for (const std::string& f : previewStills) {
        std::string err;
        if (!f.empty() && readPngRgba(f, prevBgW_, prevBgH_, prevBg_, err) && prevBgW_ > 0 && prevBgH_ > 0)
            return;
    }
    prevBg_ = spr_.image(0xD1);
    prevBgW_ = spr_.width(0xD1), prevBgH_ = spr_.height(0xD1);
    if (prevBg_.size() < size_t(prevBgW_) * size_t(prevBgH_) * 4)
        prevBg_.clear(), prevBgW_ = prevBgH_ = 0;
}

// ---- the background: the hall (0xD1) slowly drifting, grey and dark, a vignette, blood at the edges ----
bool Frontend::rBackground()
{
    const int W = cw_, H = kHeight;
    if (hall_.empty()) {
        hall_ = spr_.image(0xD1);
        logo_ = spr_.image(0xD4);
        logoW_ = spr_.width(0xD4), logoH_ = spr_.height(0xD4);
        if (logo_.size() < size_t(logoW_) * size_t(logoH_) * 4) logoW_ = 0;
        std::string err;
        if (wordmarkFile.empty() || !readPngRgba(wordmarkFile, wordW_, wordH_, word_, err))
            wordW_ = 0;   // (no wordmark: the logo alone)
    }
    const int hw = spr_.width(0xD1), hh = spr_.height(0xD1);
    if (splatW_ != W) {   // the vignette (per width; remake2: the edges' blood is the foreground's, drawRPanel)
        splatW_ = W;
        splat_.assign(size_t(W) * size_t(H), 0);
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                const float vx = (float(x) - W * 0.5f) / (W * 0.5f), vy = (float(y) - H * 0.5f) / (H * 0.5f);
                const float v = 1.0f - 0.6f * std::pow(clamp01((std::sqrt(vx * vx * 0.8f + vy * vy) - 0.35f) / 0.85f), 1.4f);
                splat_[size_t(y) * W + x] = uint8_t(v * 255.0f + 0.5f);
            }
    }
    const float t = float(nowMs_) / 1000.0f;
    // (remakefix, user 2026-10-22: Quit keeps the main menu's tint, not the settings pages' stronger one; remakepages:
    // the card pages untinted too)
    const bool red = screen_ != 1 && screen_ != 21 && !rPlain_;   // the settings pages: a red-tinted backdrop (as the Remake's)
    // the hall slowly drifting (liveattract: on the GPU the attract running live instead, when the host runs it)
    const int64_t key = int64_t(nowMs_ / 50) * 4 + (red ? 1 : 0) + (int64_t(W) << 48);   // (a new picture 20 times a second)
    const float zoom = 1.08f + 0.04f * std::sin(t * 0.05f);
    const float s = std::max(float(W) / float(std::max(1, hw)), float(H) / float(std::max(1, hh))) * zoom;
    const float ox = (float(hw) * s - float(W)) / 2 + std::sin(t * 0.031f) * (float(hw) * s - float(W)) * 0.45f;
    const float oy = (float(hh) * s - float(H)) / 2 + std::cos(t * 0.023f) * (float(hh) * s - float(H)) * 0.45f;
    const bool art = hw > 0 && hall_.size() >= size_t(hw) * size_t(hh) * 4;
    float mul[3], add[3];
    backdropTint(red, mul, add);
    if (gpuLayers) {   // blood2: the GPU's backdrop (Hud::drawRemake): the source, where it lies, the tint; no pixels here
        RemakeLayers& L = layers_;
        for (int k = 0; k < 3; ++k) L.mul[k] = mul[k], L.add[k] = add[k];
        // liveattract: the live picture, sharp (user 2026-10-21: as the Remake's, little or no blur): the window's own
        // pixels across the screen, cut to its shape when the screen is narrower / wider (rows bottom-up)
        auto cover = [&](int tw, int th, float uv[4]) {
            const float ta = float(tw) / float(std::max(1, th)), sa = float(W) / float(H);
            const float cu = ta > sa ? sa / ta : 1.0f, cv = ta > sa ? 1.0f : ta / sa;
            uv[0] = 0.5f - cu / 2, uv[2] = 0.5f + cu / 2;
            uv[1] = 0.5f + cv / 2, uv[3] = 0.5f - cv / 2;
        };
        // remakefix: the picture it fades from (a scene cut, the start's still; 0: the dark fill)
        L.mix = std::clamp(liveMix, 0.0f, 1.0f);
        L.tex2 = liveOldTex;
        if (liveOldTex && liveOldW > 0 && liveOldH > 0)
            cover(liveOldW, liveOldH, L.uv2);
        L.fill = false;
        if (liveTex && liveW > 0 && liveH > 0) {
            L.tex = liveTex, L.src = nullptr, L.grey = false;
            L.sw = liveW, L.sh = liveH, L.srcGen = 0;
            float uv[4];
            cover(liveW, liveH, uv);
            L.u0 = uv[0], L.v0 = uv[1], L.u1 = uv[2], L.v1 = uv[3];
            return false;
        }
        L.mix = 1.0f;
        if (liveHold) {   // remakefix: the live attract on its way: the dark fill (never the hall's stretched picture)
            L.tex = 0, L.src = nullptr, L.sw = L.sh = 0, L.srcGen = 0, L.fill = true;
            return false;
        }
        L.tex = 0;
        L.sw = hw, L.sh = hh, L.grey = false;
        L.src = art ? hall_.data() : nullptr;
        L.srcGen = art ? 1 : 0;
        if (!L.src)
            L.sw = L.sh = 0;
        L.u0 = ox / s / float(std::max(1, hw)), L.u1 = (float(W) + ox) / s / float(std::max(1, hw));
        L.v0 = oy / s / float(std::max(1, hh)), L.v1 = (float(H) + oy) / s / float(std::max(1, hh));
        return false;
    }
    if (key == bgKey_ && bgHue_ == bloodHue_ && bgLine_.size() == size_t(W) * size_t(H) * 3)
        return false;
    bgKey_ = key;
    bgHue_ = bloodHue_;
    bgLine_.resize(size_t(W) * size_t(H) * 3);
    for (int y = 0; y < H; ++y) {
        uint8_t* line = &bgLine_[size_t(y) * W * 3];
        const float sy = (float(y) + oy) / s - 0.5f;
        for (int x = 0; x < W; ++x) {
            float c[3] = {20, 16, 16};
            const float sx = (float(x) + ox) / s - 0.5f;
            if (art) {
                const int x0 = std::clamp(int(sx), 0, hw - 1), y0 = std::clamp(int(sy), 0, hh - 1);
                const int x1 = std::min(x0 + 1, hw - 1), y1 = std::min(y0 + 1, hh - 1);
                const float ax = clamp01(sx - float(x0)), ay = clamp01(sy - float(y0));
                for (int k = 0; k < 3; ++k) {
                    auto px = [&](int xx, int yy) { return float(hall_[(size_t(yy) * hw + xx) * 4 + k]); };
                    c[k] = (px(x0, y0) * (1 - ax) + px(x1, y0) * ax) * (1 - ay) + (px(x0, y1) * (1 - ax) + px(x1, y1) * ax) * ay;
                }
            }
            const float l = 0.3f * c[0] + 0.59f * c[1] + 0.11f * c[2];
            for (int k = 0; k < 3; ++k) c[k] = (l * 0.8f + c[k] * 0.2f) * 0.62f;   // grey, dark
            for (int k = 0; k < 3; ++k) c[k] = c[k] * mul[k] + add[k];   // (the settings pages: tinted)
            const float v = splat_[size_t(y) * W + x] / 255.0f;
            for (int k = 0; k < 3; ++k)
                line[size_t(x) * 3 + k] = uint8_t(std::clamp(c[k] * v, 0.0f, 255.0f));
        }
    }
    return true;
}

// a Remake screen's frame, in layers each redrawn only when it changed: the background (bgLine_:
// the hall's drift), the widgets without the hover (fgStatic_: a new tree, the prompts' keys), the widgets under the
// hand / focus and the help line over that (fg_), their composite (comp_); then the canvas = comp_ + the hand
void Frontend::remakeRender()
{
    std::vector<uint8_t>& canvas = *canvas_;
    const int PW = rpw(), PH = rph();
    const size_t bytes = size_t(PW) * size_t(PH) * 4;
    rOffX_ = rOffY_ = 0, rW_ = PW, rH_ = PH;
    bloodHue_ = bloodColour();
    bool dirty = rBackground();
    // the widgets under the hand / the focus (the hover walk's marks), then the marks off (the draw walk's clearing)
    std::vector<Widget*> lit;
    std::vector<std::pair<Widget*, bool>> live;   // menuperf: the changing widgets (each its own part), lit or not
    std::vector<Widget*> over;                     // optui: drawn over the lit ones (the crosshair preview)
    std::function<void(Widget*)> collect = [&](Widget* w) {
        for (; w; w = w->next) {
            if (w->over) over.push_back(w);
            else if (w->live) live.push_back({w, (w->flags & 0x40000) != 0});
            else if (w->flags & 0x40000) lit.push_back(w);
            w->flags &= ~0x40000u;
            collect(w->child);
        }
    };
    collect(root_);
    const std::string ssig = std::to_string(treeGen_) + "|" + std::to_string(PW) + "x" + std::to_string(PH) + (padPrompts_ ? "p" + std::to_string(padType_) : std::string("k")) + "b" + std::to_string(buttonStyle) + (iconOutline ? "o" : "") +
                             std::to_string(bloodHue_) + (gpuLayers ? "g" : "s" + std::to_string(splatSeed)) +
                             (lang::active() ? "L" + std::to_string(lang::gen()) : std::string());
    if (ssig != staticSig_ || fgStatic_.size() != bytes) {
        // optui (menu perf): a settings page built again (a list scrolled, a dropdown opened / closed, a value changed):
        // only the part where widgets changed is drawn again (the widgets there, clipped to it); another page, size,
        // colour or prompts: all of it
        std::vector<StaticW> nowWs;
        bool flat = root_ != nullptr;
        if (root_) {
            auto add = [&](const Widget* w) {
                if (w->live) {   // (leftovers: a live widget is its own part, never in this layer: no change here)
                    nowWs.push_back({"live", {0, 0, 0, 0}});
                    flat = flat && !w->child;
                    return;
                }
                std::string k(reinterpret_cast<const char*>(&w->draw), sizeof w->draw);
                char b[160];
                std::snprintf(b, sizeof b, "|%d,%d,%d,%d|%d|%d|%d|%X|%X|%d%d", w->x, w->y, w->w, w->h, w->user, w->seed, int(w->id),
                              w->flags & ~0x40000u, w->tint, int(w->live), int(w->over));
                k += b + w->text + "|" + w->value;
                const int mg = 24;
                nowWs.push_back({k, {w->x - mg, w->y - mg, w->x + w->w + mg, w->y + w->h + mg}});
                flat = flat && !w->child;
            };
            nowWs.push_back({"root" + std::to_string(rPanel_[0]) + "," + std::to_string(rPanel_[1]) + "," + std::to_string(rPanel_[2]) + "," +
                                 std::to_string(rPanel_[3]) + "," + std::to_string(root_->user) + "|" + std::to_string(rPage_[0]) + "," +
                                 std::to_string(rPage_[1]) + "," + std::to_string(rPage_[2]) + "," + std::to_string(rPage_[3]) + "," +
                                 std::to_string(rPage_[4]),
                             {0, 0, cw_, kHeight}});
            for (const Widget* w = root_->child; w; w = w->next)
                add(w);
        }
        const std::string skey = ssig.substr(ssig.find('|')) + "#" + remakeKey_;   // (the signature without the tree's number)
        int d[4] = {1 << 30, 1 << 30, -(1 << 30), -(1 << 30)};   // the changed part (units)
        // (leftovers: the Jukebox too: its list's page turned redraws the rows' part, not the whole page)
        bool partial = flat && fgStatic_.size() == bytes && skey == staticKey_ &&
                       (remakeKey_.rfind("set|", 0) == 0 || remakeKey_ == "jukebox") &&
                       !staticWs_.empty() && !nowWs.empty() && staticWs_[0].key == nowWs[0].key;
        if (partial) {
            std::multiset<std::string> before, after;
            for (const StaticW& s : staticWs_) before.insert(s.key);
            for (const StaticW& s : nowWs) after.insert(s.key);
            auto grow = [&](const StaticW& s) {
                if (s.r[2] <= s.r[0])
                    return;   // (a live widget: nothing in this layer)
                d[0] = std::min(d[0], s.r[0]), d[1] = std::min(d[1], s.r[1]), d[2] = std::max(d[2], s.r[2]), d[3] = std::max(d[3], s.r[3]);
            };
            for (const StaticW& s : staticWs_)
                if (const auto it = after.find(s.key); it != after.end()) after.erase(it); else grow(s);
            for (const StaticW& s : nowWs)
                if (const auto it = before.find(s.key); it != before.end()) before.erase(it); else grow(s);
            partial = d[2] - d[0] < cw_ * 9 / 10 || d[3] - d[1] < kHeight * 9 / 10;   // (nearly all: drawn whole)
        }
        staticSig_ = ssig;
        staticKey_ = skey;
        staticWs_ = std::move(nowWs);
        fgSig_.clear();
        if (partial && d[2] <= d[0]) {
            // (nothing drawn differs: the layer as it is)
        } else if (partial) {
            const int X0 = std::clamp(int(float(d[0]) * ps_), 0, PW), Y0 = std::clamp(int(float(d[1]) * ps_), 0, PH);
            const int X1 = std::clamp(int(float(d[2]) * ps_) + 1, 0, PW), Y1 = std::clamp(int(float(d[3]) * ps_) + 1, 0, PH);
            std::vector<uint8_t> sub(size_t(std::max(0, X1 - X0)) * size_t(std::max(0, Y1 - Y0)) * 4, 0);
            canvas_ = &sub, rOffX_ = X0, rOffY_ = Y0, rW_ = X1 - X0, rH_ = Y1 - Y0;
            walking_ = true;
            stackReset();
            if (root_->draw)
                (this->*root_->draw)(*root_);
            push(*root_);
            size_t k = 1;
            for (Widget* w = root_->child; w && walking_; w = w->next, ++k) {
                const int* r = staticWs_[k].r;
                if (r[2] <= d[0] || r[0] >= d[2] || r[3] <= d[1] || r[1] >= d[3])
                    continue;   // (outside the changed part)
                if (w->draw && !w->live && !w->over && (!(w->flags & 0x4000) || blink_))
                    drawW(*w);
            }
            walking_ = false;
            for (int y = Y0; y < Y1; ++y)
                std::memcpy(&fgStatic_[(size_t(y) * size_t(PW) + size_t(X0)) * 4], &sub[size_t(y - Y0) * size_t(X1 - X0) * 4],
                            size_t(X1 - X0) * 4);
            canvas_ = &canvas, rOffX_ = rOffY_ = 0, rW_ = PW, rH_ = PH;
            ++staticGen_;
            staticDirty_[0] = X0, staticDirty_[1] = Y0, staticDirty_[2] = X1, staticDirty_[3] = Y1;   // (leftovers: the host uploads this part)
        } else {
            fgStatic_.assign(bytes, 0);
            canvas_ = &fgStatic_;
            walking_ = true;
            stackReset();
            if (root_)
                drawWalk(root_);
            walking_ = false;
            canvas_ = &canvas;
            ++staticGen_;
            staticDirty_[0] = staticDirty_[1] = staticDirty_[2] = staticDirty_[3] = 0;   // (all of it)
        }
    }
    std::string sig = ssig + "|" + help_;
    for (const Widget* w : lit)
        sig += "|" + std::to_string(reinterpret_cast<uintptr_t>(w));
    for (const auto& [w, on] : live)
        sig += "|L" + std::to_string(reinterpret_cast<uintptr_t>(w)) + (on ? "+" : "-") + std::to_string(w->user) + w->text;
    for (const Widget* w : over)
        sig += "|O" + std::to_string(reinterpret_cast<uintptr_t>(w)) + w->value;
    // fix28: the comparison pictures: shown with their row's focus; the maker's number polled 3 times a second (a
    // picture finished: read again, drawn again)
    if (!rPreviewRow_.empty() && previewGen && nowMs_ >= previewPollMs_) {
        previewPollMs_ = nowMs_ + 333;
        if (const int g = previewGen(); g != previewGenSeen_) {
            previewGenSeen_ = g;
            previewPics_.clear();
        }
    }
    auto previewHidden = [&](const Widget* w) {
        const auto it = rPreviewRow_.find(w);
        return it != rPreviewRow_.end() && std::find(lit.begin(), lit.end(), it->second) == lit.end();
    };
    if (!rPreviewRow_.empty()) {
        sig += "|P" + std::to_string(previewGenSeen_);
        for (const auto& [pw, row] : rPreviewRow_)   // (relfix2: Hit Marks' / Transparency's picture flips each second)
            if (!previewHidden(pw) && rPreviewRows_ && pw->user >= 0 && pw->user < int(rPreviewRows_->size()) &&
                !(*rPreviewRows_)[size_t(pw->user)].previewLook.empty())
                sig += "|F" + std::to_string(nowMs_ / 1000u);
    }
    if (sig != fgSig_|| (!gpuLayers && fg_.size() != bytes)) {
        fgSig_ = sig;
        // the CPU: the whole foreground (the static layer + these); the GPU (blood2): each in its own small part
        // (x, y, w, h units with room for its glow / marks) over the static layer, so a focus change is cheap
        // (menuperf: a lit widget's / help line's part kept for this static layer: drawn once, then copied; key "": not kept)
        hov_.clear();
        // (optui: a settings page's parts are kept by what they show while the page is the same, across its builds: a
        // scroll / a dropdown closing finds the lit row's part again)
        const bool byLook = remakeKey_.rfind("set|", 0) == 0;
        const std::string pkey = byLook ? staticKey_ : ssig;
        if (partCacheSig_ != pkey) {
            partCacheSig_ = pkey;
            partCache_.clear();
            partCacheBytes_ = 0;
        }
        auto part = [&](const std::string& key, float x, float y, float w, float h, const std::function<void()>& draw) {
            if (!gpuLayers) {
                draw();
                return;
            }
            if (!key.empty())
                if (const auto it = partCache_.find(key); it != partCache_.end()) {
                    hov_.push_back(it->second);
                    return;
                }
            RemakeLayers::Part t;
            t.x0 = std::max(0, int(x * ps_)), t.y0 = std::max(0, int(y * ps_));
            t.w = std::min(int((x + w) * ps_) + 1, PW) - t.x0, t.h = std::min(int((y + h) * ps_) + 1, PH) - t.y0;
            if (t.w <= 0 || t.h <= 0)
                return;
            t.rgba.assign(size_t(t.w) * size_t(t.h) * 4, 0);
            canvas_ = &t.rgba, rOffX_ = t.x0, rOffY_ = t.y0, rW_ = t.w, rH_ = t.h;
            draw();
            canvas_ = &canvas, rOffX_ = rOffY_ = 0, rW_ = PW, rH_ = PH;
            constexpr size_t kPartCacheBytes = size_t(64) << 20;
            if (!key.empty() && partCacheBytes_ + t.rgba.size() <= kPartCacheBytes) {
                partCacheBytes_ += t.rgba.size();
                partCache_.emplace(key, t);
            }
            hov_.push_back(std::move(t));
        };
        if (!gpuLayers) {
            fg_ = fgStatic_;
            canvas_ = &fg_;
        }
        stackReset();
        for (const auto& [w, on] : live)   // menuperf: the live widgets (under the lit ones; room for the text's edge; kept but the
            part(w == jbTime_ || w == jbBar_ ? std::string() :    // time and the bar, which change each second)
                     "L" + std::to_string(reinterpret_cast<uintptr_t>(w)) + (on ? "+" : "-") + std::to_string(w->user) + w->text,
                 float(w->x - 40), float(w->y - 10), float(w->w + 80), float(w->h + 20), [&, w = w, on = on]() {
                if (on) w->flags |= 0x40000;
                if (w->draw)
                    drawW(*w);
                w->flags &= ~0x40000u;
            });
        for (Widget* w : lit)   // (the Remake widgets are the root's children: their rectangles are the screen's)
            part(byLook ? "w" + std::string(reinterpret_cast<const char*>(&w->draw), sizeof w->draw) + std::to_string(w->x) + "," +
                              std::to_string(w->y) + "," + std::to_string(w->w) + "," + std::to_string(w->h) + "," +
                              std::to_string(w->user) + "," + std::to_string(w->seed) + "," + std::to_string(w->id) + w->text + "|" + w->value
                        : "w" + std::to_string(reinterpret_cast<uintptr_t>(w)), float(w->x - 48), float(w->y - 24), float(w->w + 96), float(w->h + 48), [&]() {
                w->flags |= 0x40000;
                if (w->draw)
                    drawW(*w);
                w->flags &= ~0x40000u;
            });
        for (Widget* w : over) {   // optui: over the focus bar (menufb: the Firing Range; kept per what it shows)
            if (w->w <= 0 || w->h <= 0 || previewHidden(w))
                continue;   // (a range crosshair not aiming; fix28: a preview without its row's focus)
            const bool pv = rPreviewRow_.count(w) != 0;   // (fix28: a preview: kept per its row's value and the pictures)
            const std::string pkey = pv && rPreviewRows_ && w->user < int(rPreviewRows_->size()) && (*rPreviewRows_)[size_t(w->user)].value
                                         ? "P" + std::to_string(previewGenSeen_) + (*rPreviewRows_)[size_t(w->user)].value() +
                                               ((*rPreviewRows_)[size_t(w->user)].previewLook.empty() ? std::string()
                                                                                                       : "F" + std::to_string(nowMs_ / 1000u % 6u))
                                         : std::string();
            part(w->live ? std::string() : "o" + std::to_string(w->x) + "," + std::to_string(w->y) + "," + std::to_string(w->w) + "," + std::to_string(w->user) + w->value + pkey, float(w->x - 8), float(w->y - 8), float(w->w + 16),
                 float(w->h + 16), [&]() {
                if (w->draw)
                    drawW(*w);
            });
        }
        if (!help_.empty()) part("h" + help_, 0.0f, float(rHelp_[1] - 20), float(cw_), float(kHeight - rHelp_[1] + 20), [&]() {   // the help line (two lines when too wide)
            // polish (user 2026-11-03: the Gallery's top hint ran into the layout): at the top of the page (the Gallery,
            // the Cut-scenes) a little smaller, wrapped to its width in as many lines as it needs, downwards
            const bool topLine = rHelp_[1] < 40;
            const float th = topLine ? 10.0f : 11.0f;   // (blood2: was 9)
            // lang: the translation, cut into lines by the language's widths (English: as before)
            const std::string help = T(help_);
            const RawText raw(this);
            std::vector<std::string> lines{help};
            if (topLine) {
                lines = lang::wrapBy(help, float(rHelp_[2]), [&](const std::string& s) { return rTextWidthRaw(s, th); });
                if (langCheck && lines.size() > 3)
                    langOverflow(help, 0, cw_ * 2, cw_, nullptr);
                float y = float(rHelp_[1]) - 4.0f;
                for (const std::string& l : lines) {
                    rText(l, float(rHelp_[0]), y, th, 0xB4AAA2, 1, 0.0f, 0, kTxOutline | kTxShadow);
                    y += th + 3;
                }
                return;
            }
            if (lang::active()) {
                lines = lang::wrapBy(help, float(rHelp_[2]), [&](const std::string& s) { return rTextWidthRaw(s, th); });
                if (langCheck && lines.size() > 2)
                    langOverflow(help, 0, cw_ * 2, cw_, nullptr);
            } else if (rTextWidth(help_, th) > float(rHelp_[2])) {
                size_t cut = std::string::npos;
                for (size_t i = 0; i < help_.size(); ++i)
                    if (help_[i] == ' ' && (cut == std::string::npos ||
                                            std::abs(int(i) - int(help_.size() / 2)) < std::abs(int(cut) - int(help_.size() / 2))))
                        cut = i;
                if (cut != std::string::npos)
                    lines = {help_.substr(0, cut), help_.substr(cut + 1)};
            }
            float y = float(rHelp_[1]) - (lines.size() > 1 ? 6.0f : 0.0f);
            for (const std::string& l : lines) {
                rText(l, float(rHelp_[0]), y, th, 0xB4AAA2, 1, 0.0f, 0, kTxOutline | kTxShadow);
                y += th + 4;
            }
        });
        dirty = true;
        ++fgGen_;
    }
    canvas_ = &canvas;
    if (gpuLayers) {   // blood2: the GPU composes (Hud::drawRemake): the layers, no picture here
        RemakeLayers& L = layers_;
        L.fg = &fgStatic_, L.fw = PW, L.fh = PH, L.fgGen = staticGen_, L.unitsW = cw_;
        std::copy(staticDirty_, staticDirty_ + 4, L.fgDirty);
        L.parts = &hov_, L.partsGen = fgGen_;
        L.fade = menuFade_;
        // the splatter: drawn on a worker when its pattern, colour or size changes (~0.2 s), shown when done
        const std::string want = std::to_string(cw_) + "," + std::to_string(PH) + "," + std::to_string(splatSeed) + "," + std::to_string(bloodHue_);
        if (splatJob_.valid() && splatJob_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            splatImg_ = splatJob_.get();
            splatImgW_ = splatJobW_, splatImgH_ = splatJobH_;
            splatKey_ = splatJobKey_;
            ++splatGen_;
        }
        if (want != splatKey_ && !splatJob_.valid()) {
            splatJobKey_ = want, splatJobW_ = PW, splatJobH_ = PH;
            splatJob_ = std::async(std::launch::async, &Frontend::splatterLayer, cw_, ps_, splatSeed, bloodHue_);
        }
        L.splat = splatImg_.empty() ? nullptr : &splatImg_;
        L.spw = splatImgW_, L.sph = splatImgH_, L.splatGen = splatGen_;
        // regallery: the Remake Gallery's pages: no live backdrop (the Gallery: its own stage under the page; the
        // Cut-scenes page: the dark fill), no splatter, the file panel and the list as cross-fading parts
        L.noBackdrop = galleryRE() && screen_ == 17;
        L.anim = nullptr;
        if (galleryRE()) {
            L.splat = nullptr;
            L.tex = 0, L.src = nullptr, L.fill = false, L.mix = 1.0f;
            for (int k = 0; k < 3; ++k)
                L.mul[k] = 1.0f, L.add[k] = 0.0f;
            reAnimate();
            L.anim = &reParts_, L.animGen = reGen_;
        }
        L.hand = nullptr;
        if (handShown_ && !hideHand && !rangeHand_) {   // (menufb: in the Firing Range the crosshair, no hand)
            const int id = 0xF9 + cursorFrame_;
            if (id != handImgId_) {
                handImgId_ = id;
                handImg_ = spr_.image(id);
            }
            const int iw = spr_.width(id), ih = spr_.height(id);
            if (iw > 0 && ih > 0 && handImg_.size() >= size_t(iw) * size_t(ih) * 4)
                L.hand = &handImg_, L.hiw = iw, L.hih = ih, L.handId = id, L.hx = float(px_), L.hy = float(py_), L.handAlpha = handAlpha;
        }
        return;
    }
    if (dirty || comp_.size() != bytes) {   // the foreground over the background (scaled up S times)
        comp_.resize(bytes);
        for (int y = 0; y < PH; ++y) {
            const uint8_t* bg = &bgLine_[size_t(std::min(kHeight - 1, int(float(y) / ps_))) * size_t(cw_) * 3];
            const uint8_t* f = &fg_[size_t(y) * size_t(PW) * 4];
            uint8_t* d = &comp_[size_t(y) * size_t(PW) * 4];
            for (int x = 0; x < PW; ++x, f += 4, d += 4) {
                const uint8_t* b = bg + size_t(std::min(cw_ - 1, int(float(x) / ps_))) * 3;
                const uint32_t a = f[3];
                if (a == 0)
                    d[0] = b[0], d[1] = b[1], d[2] = b[2];
                else if (a == 255)
                    d[0] = f[0], d[1] = f[1], d[2] = f[2];
                else
                    for (int k = 0; k < 3; ++k)
                        d[k] = uint8_t((f[k] * a + b[k] * (255 - a) + 127) / 255);
                d[3] = 255;
            }
        }
    }
    if (canvas.size() != bytes)
        canvas.resize(bytes);
    std::memcpy(canvas.data(), comp_.data(), bytes);
    if (handShown_ && !rangeHand_)
        drawHand(px_, py_);   // the bony hand
}

// fix28 (user 2026-10-27: "it would be cool if some settings show you comparison images like models for example"): the
// focused row's comparison pictures (Extra::preview: the game's own renders of one Chapter 1 frame with each value) on
// a small torn panel, each with its value's name under it, the current one framed red; not made yet: "Making the
// picture..." (the host renders them in the background)
void Frontend::drawRPreview(Widget& w)
{
    if (!rPreviewRows_ || w.user < 0 || w.user >= int(rPreviewRows_->size()))
        return;
    const Extra& e = (*rPreviewRows_)[size_t(w.user)];
    if (!e.preview || e.choices.size() < 2)
        return;
    int x, y, ww, hh;
    absRect(w, x, y, ww, hh);
    const int nc = int(e.choices.size());
    const std::string cur = e.value ? e.value() : std::string();
    int curK = e.choiceNow ? e.choiceNow() : -1;
    for (int k = 0; k < nc && curK < 0; ++k)
        if (e.choices[size_t(k)] == cur)
            curK = k;
    std::vector<int> ks = e.previewList;
    if (!ks.empty()) {
        if (std::find(ks.begin(), ks.end(), curK) == ks.end())
            curK = ks[0];   // (fxux: Auto shown as the choice it is now)
    } else if (nc <= 3)
        for (int k = 0; k < nc; ++k) ks.push_back(k);
    else
        ks.push_back(std::max(0, curK));
    const float np = float(std::max<size_t>(ks.size(), 1)), S = ps_;
    auto picOf = [&](int k, std::string& file) -> PreviewPic* {
        file = e.preview(k);
        PreviewPic* pic = nullptr;
        if (!file.empty()) {
            auto it = previewPics_.find(file);
            if (it == previewPics_.end()) {
                PreviewPic p;
                std::string err;
                if (file.rfind("mem:", 0) == 0) {   // menus3: made in memory (the custom colour's: one per colour)
                    if (previewPics_.size() > 24)   // (a dragged colour bar: the old ones dropped)
                        previewPics_.clear();
                    if (previewImage && previewImage(file.substr(4), p.rgba, p.w, p.h))
                        it = previewPics_.emplace(file, std::move(p)).first;
                } else {
                    if (!readPngRgba(file, p.w, p.h, p.rgba, err))
                        p = PreviewPic{};
                    it = previewPics_.emplace(file, std::move(p)).first;
                }
            }
            if (it != previewPics_.end() && it->second.w > 0)
                pic = &it->second;
        }
        return pic;
    };
    auto frame = [&](float tx, float ty, float tw, float th, bool on) {   // red round the current value's, dark round the others
        for (int py = int((ty - 2) * S); py < int((ty + th + 2) * S); ++py)
            for (int px = int((tx - 2) * S); px < int((tx + tw + 2) * S); ++px)
                rBlend(px, py, on ? 0xC01810 : 0x0C0808, 1.0f);
    };
    auto none = [&](const std::string& file, float tx, float ty, float tw, float th) {
        for (int py = int(ty * S); py < int((ty + th) * S); ++py)
            for (int px = int(tx * S); px < int((tx + tw) * S); ++px)
                rBlend(px, py, 0x181212, 1.0f);
        rText(file.empty() && e.preview(-1).empty() ? "No picture" : "Making the picture...", tx + tw / 2, ty + th / 2 - 6, 11.0f,
              kRowOff, 1);
    };
    auto caption = [&](const std::string& s, float cx, float cy, float maxW, float size, uint32_t ink) {   // (fits its picture)
        const float cw = rTextWidth(s, size), ch = cw > maxW + 4.0f ? std::max(7.0f, size * (maxW + 4.0f) / cw) : size;
        rText(s, cx, cy + (size - ch) / 2, ch, ink, 1, 0.0f, 0, kTxShadow);
    };
    // relfix2 (user 2026-11-03: side by side "the differences aren't noticeable to a player"): Hit Marks / Transparency
    // (previewLook): one picture that flips between the choices each second (its spot for the close-ups outlined) and a
    // close-up of each choice: the same small spot, where the pictures differ most, at 3-4 screen pixels a picture
    // pixel (nearest, the dots as dots), each with its name and what to look for
    if (e.previewLook.size() >= size_t(nc) && ks.size() >= 2) {
        const float th = float(hh) - 30.0f, lw = th * 16.0f / 9.0f, ty = float(y + 5);
        const float zw = std::min(th * 1.5f, (float(ww) - 24.0f - lw - 6.0f * (np - 1.0f)) / np);   // (a small spot: the dots close)
        const float total = 12.0f + lw + 12.0f + np * zw + 6.0f * (np - 1.0f), x0 = float(x) + (float(ww) - total) / 2.0f;
        rTorn(x0, float(y), total, float(hh), 17 + w.user, false, 0.96f);
        std::vector<PreviewPic*> pics;
        std::vector<std::string> files(ks.size());
        for (size_t j = 0; j < ks.size(); ++j)
            pics.push_back(picOf(ks[j], files[j]));
        // the close-ups' spot (in picture pixels): the crop of zw x th units at z screen pixels a pixel
        const int z = S >= 2.0f ? 4 : 3;
        bool same = pics[0] != nullptr;
        for (PreviewPic* p : pics)
            same = same && p && p->w == pics[0]->w && p->h == pics[0]->h && p->rgba.size() == pics[0]->rgba.size();
        int cwB = 0, chB = 0, zx = 0, zy = 0;
        if (same) {
            const int W = pics[0]->w, H = pics[0]->h;
            cwB = std::clamp(int(zw * S) / z, 8, W), chB = std::clamp(int(th * S) / z, 8, H);
            std::string zkey = std::to_string(cwB) + "x" + std::to_string(chB);
            for (const std::string& f : files) zkey += "|" + f;
            auto at = pics[0]->zoom.find(zkey);
            if (at == pics[0]->zoom.end()) {
                // the pixels where the pictures differ (by more than a little), then the crop-sized window with the most
                std::vector<int64_t> sum(size_t(W + 1) * size_t(H + 1), 0);
                for (int yy = 0; yy < H; ++yy)
                    for (int xx = 0; xx < W; ++xx) {
                        int d = 0;
                        const size_t o = (size_t(yy) * size_t(W) + size_t(xx)) * 4;
                        for (size_t j = 1; j < pics.size(); ++j) {
                            int dj = 0;
                            for (int c = 0; c < 3; ++c)
                                dj += std::abs(int(pics[j]->rgba[o + size_t(c)]) - int(pics[0]->rgba[o + size_t(c)]));
                            d += dj > 24 ? 1 : 0;
                        }
                        sum[size_t(yy + 1) * size_t(W + 1) + size_t(xx + 1)] = d + sum[size_t(yy) * size_t(W + 1) + size_t(xx + 1)] +
                                                                              sum[size_t(yy + 1) * size_t(W + 1) + size_t(xx)] -
                                                                              sum[size_t(yy) * size_t(W + 1) + size_t(xx)];
                    }
                int64_t best = -1;
                std::pair<int, int> pick{(W - cwB) / 2, (H - chB) / 2};
                for (int yy = 0; yy + chB <= H; yy += 2)
                    for (int xx = 0; xx + cwB <= W; xx += 2) {
                        auto S2 = [&](int a, int b) { return sum[size_t(b) * size_t(W + 1) + size_t(a)]; };
                        const int64_t v = S2(xx + cwB, yy + chB) - S2(xx, yy + chB) - S2(xx + cwB, yy) + S2(xx, yy);
                        if (v > best)
                            best = v, pick = {xx, yy};
                    }
                at = pics[0]->zoom.emplace(zkey, pick).first;
            }
            zx = at->second.first, zy = at->second.second;
        }
        // the flipping picture
        const size_t shown = size_t(nowMs_ / 1000u) % ks.size();
        const float lx = x0 + 6.0f;
        frame(lx, ty, lw, th, ks[shown] == curK);
        if (pics[shown]) {
            rImage(pics[shown]->rgba, pics[shown]->w, pics[shown]->h, lx, ty, lw, th, 1.0f);
            if (same) {   // (where the close-ups are from: a thin light outline)
                const float sx = lw / float(pics[0]->w), sy = th / float(pics[0]->h);
                const int X0 = int((lx + float(zx) * sx) * S), Y0 = int((ty + float(zy) * sy) * S);
                const int X1 = int((lx + float(zx + cwB) * sx) * S), Y1 = int((ty + float(zy + chB) * sy) * S);
                for (int px = X0; px <= X1; ++px)
                    rBlend(px, Y0, 0xF0E8D0, 0.9f), rBlend(px, Y1, 0xF0E8D0, 0.9f);
                for (int py = Y0; py <= Y1; ++py)
                    rBlend(X0, py, 0xF0E8D0, 0.9f), rBlend(X1, py, 0xF0E8D0, 0.9f);
            }
        } else
            none(files[shown], lx, ty, lw, th);
        const int kS = ks[shown];
        caption(e.choices[size_t(kS)], lx + lw / 2, ty + th + 4, lw, 11.0f, kS == curK ? kInkLit : kRowText);
        caption(e.previewLook[size_t(kS)], lx + lw / 2, ty + th + 15, lw, 9.0f, kRowText);
        // the close-ups
        for (size_t j = 0; j < ks.size(); ++j) {
            const int k = ks[j];
            const float tx = lx + lw + 12.0f + float(j) * (zw + 6.0f);
            frame(tx, ty, zw, th, k == curK);
            if (same) {
                const PreviewPic& p = *pics[j];
                const int X0 = int(tx * S), Y0 = int(ty * S), W = int(zw * S), H = int(th * S);
                for (int jj = 0; jj < H; ++jj)
                    for (int ii = 0; ii < W; ++ii) {
                        const int sx = std::min(zx + ii / z, p.w - 1), sy = std::min(zy + jj / z, p.h - 1);
                        const uint8_t* c = &p.rgba[(size_t(sy) * size_t(p.w) + size_t(sx)) * 4];
                        rBlend(X0 + ii, Y0 + jj, (uint32_t(c[0]) << 16) | (uint32_t(c[1]) << 8) | uint32_t(c[2]), 1.0f);
                    }
            } else
                none(files[j], tx, ty, zw, th);
            caption(e.choices[size_t(k)], tx + zw / 2, ty + th + 4, zw, 11.0f, k == curK ? kInkLit : kRowText);
            caption(e.previewLook[size_t(k)], tx + zw / 2, ty + th + 15, zw, 9.0f, kRowText);
        }
        return;
    }
    // the others (Models, Blood Colour): side by side, as large as the strip lets them (16:9), centred in it
    const float th = float(hh) - 30.0f;
    const float tw = std::min(std::max(1.0f, (float(ww) - 12.0f - 6.0f * (np - 1.0f)) / np), th * 16.0f / 9.0f);
    const float total = 12.0f + np * tw + 6.0f * (np - 1.0f), x0 = float(x) + (float(ww) - total) / 2.0f;
    rTorn(x0, float(y), total, float(hh), 17 + w.user, false, 0.96f);
    for (size_t j = 0; j < ks.size(); ++j) {
        const int k = ks[j];
        const float tx = x0 + 6.0f + float(j) * (tw + 6.0f), ty = float(y + 6);
        std::string file;
        PreviewPic* pic = picOf(k, file);
        const bool on = k == curK;
        frame(tx, ty, tw, th, on);
        if (pic)
            rImage(pic->rgba, pic->w, pic->h, tx, ty, tw, th, 1.0f);
        else
            none(file, tx, ty, tw, th);
        caption(e.choices[size_t(k)], tx + tw / 2, ty + th + 5, tw, 12.0f, on ? kInkLit : kRowText);
    }
}

} // namespace hotd::game
