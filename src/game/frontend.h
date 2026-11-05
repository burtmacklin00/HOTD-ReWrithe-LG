// The PC port's front end (THOTD.EXE 1998), ported routine by routine from the decompilation and the disassembly
// (docs/decompilation.md "Front end", "Front-end input"; docs/teaser_status/menuart.md). The menus are trees of
// 0x44-byte widgets (THOTD.EXE 0x427530 ..); each screen builder makes one tree (root at 0x5CED88, screen id
// 0x5CE8A8); the pointer (0xD5A97C / 0xD5A980, 640 x 480 screen pixels) hovers / activates the widget under it; the
// bony-hand cursor is SPR.RBH sprite 0xF9 + its click frame (0..3). Text labels are ids into the string table
// 0x5CCD64 drawn in the SPR.RBH fonts (0x428494), the pictures are SPR.RBH sprites, all coloured through MENU.TBL /
// MENU.PAL (frontend_run 0x41DD3C loads menu.pal). Everything is read from the player's PC files at run time.
//
// UNVERIFIED (teaser): no routine here has a capture proof yet; the main menu was compared by eye with a PC harness
// capture (private\menuart_scratch).
#pragma once

#include "assets/sprites_pc.h"
#include "game/sights.h"
#include "game/lang.h"
#include "game/lives.h"
#include "render/ttf.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace hotd::game {

class Frontend;

// one frame of the player's input for the front end (the window's keys and mouse, already in 640 x 480 units)
struct FrontendInput {
    bool up = false, down = false, left = false, right = false;   // held: arrows / keypad 8 2 4 6 (DIK 0xC8 0x48 ...)
    bool press = false;        // an edge of Enter / keypad Enter / Space / the mouse button (0x420A60)
    bool escape = false;       // an Escape edge (frontend_input 0x41DA18: the widget tagged 1)
    bool mouseMoved = false;   // the mouse moved: the pointer goes to (mouseX, mouseY)
    bool anyKey = false;       // the start-up splash: a key edge (list 0x5CE6BC: nearly every key) or mouse button 0 / 1
    // frontend_input 0x41DA18's secret codes (docs/teaser_status/pc_cheats.md): Ctrl (DIK 0x1D / 0x9D) held, and the
    // letters pressed this frame as the PC's ASCII table 0x5CED9C gives them (upper case; list 0x5CE868)
    bool ctrl = false;
    std::string typed;
    // screen 15 (the name entry): the keyboard's keys this frame, as the PC's key handler 0x425188 reads them: a
    // character (upper case A-Z, 0-9, space; ASCII table 0x5CED9C) or one of the kKey* codes below
    std::vector<int> nameKeys;
    int mouseX = 0, mouseY = 0;
    // remakemenu (ours): the Remake style's tab keys (Q / E, Page Up / Down, a pad's shoulder buttons: an edge each),
    // and which input was used this frame (the controls prompts show its keys: pad, or the keyboard / mouse)
    bool tabPrev = false, tabNext = false;
    bool click = false;      // blood2: the press was the mouse's button (a Remake screen: on the backdrop, a new splatter)
    bool newSplat = false;   // blood2: R3 / the B key: a new splatter
    bool pad = false, keyboard = false;
    int wheel = 0;       // optui: the mouse wheel's notches this frame (+ up, - down): a settings list / dropdown scrolls
    bool alt = false;    // optui: the right mouse button's edge (the Classic style: a settings row's dropdown list)
    bool mouseHeld = false;   // menufb: the left mouse button held (a scroll bar's thumb dragged)
    int digit = 0;           // playerpick: a number key 1-4 pressed this frame (top row / keypad, no Ctrl); 0 none
    int padType = 0;         // padpass: the type (SDL_GamepadType) of the pad whose button was pressed this frame
};
enum : int { kKeyLeft = 1, kKeyRight = 2, kKeyHome = 3, kKeyEnd = 4, kKeyBack = 8, kKeyEnter = 13, kKeyDel = 127 };

// THOTD.EXE's widget (0x44 bytes); the original's offsets in the comments
struct Widget {
    int32_t tag = 0;                 // +0x00 (0x427CB0 sets it: 1 = Escape's widget, 2 = Y, 3 = N)
    uint32_t flags = 0;              // +0x04: 0x04 right / 0x20 centre (x), 0x10 bottom / 0x40 middle (y), 0x100 drawn,
                                     // 0x800 has handlers, 0x1000 label id, 0x2000 text, 0x4000 blinks, 0x40000 hovered
                                     // (set each frame by the hover walk, cleared by the draw walk), font 0x10000 /
                                     // 0x20000 (+0x40000 yellow, +0x80000 grey) / 0x400000 / 0x800000
    int32_t x = 0, y = 0, w = 0, h = 0;   // +0x08..+0x14 (relative to the parent; 0 = the parent's)
    int16_t id = -1;                 // +0x18: label id / image sprite
    int16_t idHover = -1;            // +0x1A: image sprite while hovered (-1: the same)
    std::string text;                // +0x1C (widget_set_text)
    Widget* child = nullptr;         // +0x20
    Widget* next = nullptr;          // +0x24
    int32_t user = 0;                // +0x34 (the sliders: 0 sound effects, 1 music)
    std::string help;                // ours: the one-line explanation at the screen's bottom while the pointer is on it
    bool litKids = false;            // ours (uxpolish): hovered, its children are drawn lit too (a settings row: label + value)
    bool nav = false;                // ours (remakemenu): Up / Down move the focus onto it (the Remake style's lists)
    int seed = 0;                    // ours (remakemenu): its torn edges' / stains' noise
    std::string value;               // ours (remakemenu): a settings row's value text
    std::function<void()> left;      // ours (remakemenu): Left on it (a settings row: the value before; null: activate)
    std::function<void()> right;     // ours (remakefix): Right on it (a row with a dropdown: the value after; null: activate)
    uint32_t tint = 0;               // ours (gallery3): the text drawn in this colour (0xRRGGBB; 0: as the font is)
    bool raw = false;                // ours (lang): its text is translated already (a line of a wrapped text)
    bool rtlRow = false;             // ours (rtl): a settings row's label / value: mirrored in a right-to-left language
    float ttfScale = 1.0f;           // ours (sweep2): the language's font drawn this much smaller (rows closer than its
                                     // height: the Arabic font's tall letters); the PC font as it is
    int fitW = 0;                    // ours (leftovers): a translated text (the language's font) wider than this: drawn
                                     // smaller to fit (the PC Options page's 1P / 2P cells); 0: as it is
    bool fitFont = false;            // ours (polish): an English text in the PC font wider than fitW: drawn in data/fonts'
                                     // font, smaller to fit (the PC font has no smaller size)
    bool live = false;               // ours (menuperf): a Remake screen's changing widget (the Jukebox's time, its bar):
                                     // not in the static layer, its own small part, redrawn when its text / user changes
    bool over = false;               // ours (optui): drawn over the focus (the crosshair preview): not in the static layer,
                                     // its own part after the lit widgets
    std::function<void(Widget&)> activate;               // +0x28
    void (Frontend::*draw)(Widget&) = nullptr;           // +0x2C
    bool (Frontend::*hit)(Widget&, int, int) = nullptr;  // +0x30
};

class Frontend {
public:
    static constexpr int kWidth = 640, kHeight = 480;

    // ---- remakemenu: the menu's two styles (Options > Video > Menu Style; launcher.ini menustyle) ----
    // kStyleClassic: the 1998 PC port's 640 x 480 screens (pillarboxed). kStyleRemake (LaunchConfig's default): the main menu, Quit's
    // question, Options and the settings pages laid out across a wide screen in the manner of The House of the Dead:
    // Remake (docs/menu_remake.md; its layout and behaviour only, drawn from the PC files' art and our own procedural
    // drawing in frontend_remake.cpp). Both styles show the same items, rows and actions (one model: mainMenuItems,
    // optionsRows, extras); the other screens (High Scores, Chapter Select, Gallery ...) keep the PC's layout in both.
    enum : int { kStyleClassic = 0, kStyleRemake = 1 };
    int style = kStyleClassic;
    void setStyle(int s);   // and rebuilds the screen shown when it has a Remake layout
    // the window's aspect (width / height): a Remake screen is 480 units high and as wide as the window (640 .. 1200
    // units: 4:3 .. 2.5:1); a change rebuilds it
    void setViewAspect(float aspect);
    int canvasWidth() const { return cw_; }            // the screen in menu units (the PC's 640 x 480)
    int canvasPixelsW() const { return sc_ > 1 ? rpw() : cw_ * sc_; }   // render()'s picture (a Remake screen: ps_ pixels a unit)
    int canvasPixelsH() const { return sc_ > 1 ? rph() : kHeight * sc_; }
    bool remakeScreen() const { return sc_ > 1; }
    // blood2 (the GPU menu): the window's height in pixels: a Remake screen's widgets / splatter are drawn at
    // min(it, 1440) lines (2.0 .. 3.0 pixels a unit, in quarters; at least the old 960)
    void setOutputHeight(int h);
    // blood2: a Remake screen as layers for the GPU (Hud::drawRemake): render() then only updates them (no picture in
    // the canvas). Off (the PNG tools): render() composes them on the CPU as before
    bool gpuLayers = false;
    struct RemakeLayers {
        // the backdrop: its source picture (the hall, RGBA; srcGen changes with it) or (liveattract) a GL texture of the
        // attract running live (tex, rows bottom-up: v0 / v1 given flipped), the part of it across the screen, the tint
        // after the grey / dark (c * mul + add, 0..255)
        const uint8_t* src = nullptr;
        unsigned tex = 0;
        int sw = 0, sh = 0;
        bool grey = false;
        uint64_t srcGen = 0;
        float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
        float mul[3] = {1, 1, 1}, add[3] = {0, 0, 0};
        // remakefix: the picture cross-faded from (a scene cut of the live attract, the still at the start): tex2 (0:
        // the dark fill) at its part u2 (u0, v0, u1, v1), under the backdrop drawn at mix (eased 0 .. 1; 1: no fade);
        // fill: no live picture yet (the dark fill, not the hall)
        unsigned tex2 = 0;
        float uv2[4] = {0, 0, 1, 1};
        float mix = 1.0f;
        bool fill = false;
        // the widgets and text without the focus / hover (RGBA, straight alpha, fw x fh: the screen); fgGen changes
        // when redrawn (a new page / size / colour)
        const std::vector<uint8_t>* fg = nullptr;
        int fw = 0, fh = 0;
        uint64_t fgGen = 0;
        // leftovers: what changed from fgGen - 1 to fgGen (pixels x0, y0, x1, y1; x1 <= x0: all of it): the host
        // uploads only that part when it has fgGen - 1
        int fgDirty[4] = {0, 0, 0, 0};
        // over it: the widgets under the focus / hand and the help line, each a small part at (x0, y0) pixels;
        // partsGen changes when they do
        // (alpha / dx: regallery's anim parts only: drawn at that alpha, moved dx pixels across)
        struct Part { std::vector<uint8_t> rgba; int x0 = 0, y0 = 0, w = 0, h = 0; float alpha = 1.0f, dx = 0.0f; };
        const std::vector<Part>* parts = nullptr;
        uint64_t partsGen = 0;
        // regallery: the Remake Gallery's own layers: no backdrop (the host drew the Gallery's stage and model first),
        // and its parts that cross-fade (the file panel, the entries' list; under the hover parts, animGen: redrawn)
        bool noBackdrop = false;
        const std::vector<Part>* anim = nullptr;
        uint64_t animGen = 0;
        // the hand (RGBA iw x ih, one pixel a unit) at (hx, hy) units; null: hidden
        const std::vector<uint8_t>* hand = nullptr;
        int hiw = 0, hih = 0, handId = 0;
        float hx = 0, hy = 0;
        float handAlpha = 1.0f;   // galfix: Frontend::handAlpha
        int unitsW = kWidth;   // the screen's width in units (480 high)
        float fade = 0.0f;     // the main menu idle: everything but the backdrop faded out (0 .. 1), the backdrop lighter
        // the splatter under the widgets (RGBA sw2 x sh2, straight alpha; drawn on a worker): splatGen changes with
        // it (a new pattern / colour / size: the GPU cross-fades the old one into it); null: none yet
        const std::vector<uint8_t>* splat = nullptr;
        int spw = 0, sph = 0;
        uint64_t splatGen = 0;
    };
    const RemakeLayers& remakeLayers() const { return layers_; }
    // lang (translations, game/lang.h): the language changed (Options > Language, F5): the drawn texts are redrawn
    void langChanged();
    // lang (--export-lang): every text of the screen through the lookup (the help lines, values, dropdowns' choices
    // and the grey rows' reasons too, not only the ones drawn now)
    void recordTexts();
    // --lang-check: texts wider than their box are listed in overflows (text, pixels over, the screen); a text drawn with
    // half a letter (a byte cut, misc18): over = -1
    bool langCheck = false;
    struct Overflow { std::string text; int over = 0; std::string where; };
    std::vector<Overflow> overflows;
    // testing: the centre (menu units) of the widget whose text is `text` (a Remake strip, row, tab, prompt); false: none
    bool widgetCentre(const std::string& text, int& x, int& y);
    // galfix3 (the input test): the page's i-th scroll bar: x, y, w, h, its first item (false: none)
    bool scrollBarInfo(size_t i, int (&r)[5]) const {
        if (i >= sbars_.size()) return false;
        const auto& b = sbars_[i];
        r[0] = b.x, r[1] = b.y, r[2] = b.w, r[3] = b.h, r[4] = b.first;
        return true;
    }
    // testing: the text of the Remake screen's item / row under the focus (the pointer); "" none
    std::string focusText() const;
    // testing (remakefix): a settings row's dropdown open, the bony hand shown, the Remake sounds played (remakeCue)
    bool dropdownOpen() const { return ddOpen() || cdOpen(); }
    // optui (testing, --frontend-shot HOTD_FE_DD): the settings page's row i with its dropdown open (both styles; the
    // row scrolled / paged into view)
    void openList(int row);
    bool handVisible() const { return handShown_; }
    unsigned remakeCues = 0;
    // liveattract (user 2026-10-21): the Remake screens' background is the attract running live behind the menu (the
    // host's own runtime and view, muted, only its 3D; main.cpp): the GL texture it draws into (rows bottom-up, liveW x
    // liveH pixels); 0: not running (no arcade files): the hall picture
    unsigned liveTex = 0;
    int liveW = 0, liveH = 0;
    // remakefix (user 2026-10-22: no broken / stretched frame at the start, smooth scene changes): the picture faded
    // from (liveOldTex, liveOldW x liveOldH; 0: the dark fill) and the new one's share (liveMix 0 .. 1, eased by the
    // host); liveHold: the live attract is on its way (loading): the dark fill, not the hall, until its first picture
    unsigned liveOldTex = 0;
    int liveOldW = 0, liveOldH = 0;
    float liveMix = 1.0f;
    bool liveHold = false;
    // remake2: the arcade ROM regions folder (maincpu.bin, main_data.bin): the Remake screens' blood is the arcade's own
    // hit-mark sprites (0x28..0x2D, the splash when a player is hit); none yet: the PC's copies (0x47E..0x483). A folder
    // given after the PC's were taken: the arcade's from then on
    void setArcadeRegions(const std::string& dir);

    // the sprites through MENU.TBL / MENU.PAL; false (err set) when the PC art cannot be read (the text launcher then)
    bool load(const std::string& pcInstall, std::string& err);

    // screen 1: the main menu (THOTD.EXE screen_main_menu 0x4210FC). Handlers: Arcade 0x420F70, PC Mode 0x4259A4,
    // Boss Mode 0x422B90, Ranking 0x423028, Options 0x421434, Quit 0x420F80 (tag 1: Escape).
    // ReWrithe's overhaul (user 2026-10-20: the PC's look, our structure): the same backdrop, font, list and bony hand,
    // nine items 28 pixels apart from y 0xC8 (the PC's six were 32 apart from 0xD0): the "Play" set Arcade, Chapter
    // Select (screen 9), Boss Mode, PC Mode, Co-op (onCoop: the host's page), Sonic Mode (its value; onSonic cycles),
    // then High Scores (screen 8), Options (screen 2), Quit; a one-line help at the bottom for the item under the hand
    void screenMainMenu();
    void addVersionTag();   // relprep: the version in the main menu's corner (both styles)
    // screen 21 (ours, uxpolish): Quit's "Are you sure?" with Yes (onMainMenu(5)) / No (the main menu; Escape, a pad's B)
    void screenQuit();
    // screen 22 (ours, playerpick, user 2026-10-26 "players have to go into Options to select players"): the quick
    // players choice before a local game (Arcade, Chapter Select's chapter, a Boss Mode boss, PC Mode): "Players 1 2 3
    // 4" (also the number keys 1-4, a pad's shoulder buttons), Human / CPU for each seat past player 1 (CPU: the seat's
    // level, Options > Co-op; Normal by default), Start (the focus / hand on it: one press of Enter / A / a click) and
    // Back (Escape / B: `back`). The last choice comes from playersGet (launcher.ini players= / p2bot..p4bot) and
    // Start keeps it with playersSet before `go`. playersAsk null or false (online): `go` at once
    void askPlayers(const std::string& mode, std::function<void()> go, std::function<void()> back);
    void screenPlayers();
    std::function<bool()> playersAsk;
    std::function<void(int& players, int bot[4])> playersGet;
    std::function<void(int players, const int bot[4])> playersSet;
    int pickedPlayers() const { return pickN_; }       // testing: the screen's choice
    int pickedBot(int p) const { return pickBot_[p & 3]; }
    void pickPlayers(int n, bool toStart = false);     // the count chosen (1-4; the screen rebuilt; toStart: the hand / focus onto Start)
    void pickSeat(int p);                              // seat p (1-3) Human <-> CPU
    void pickStart();                                  // Start: the choice kept (playersSet), then `go`
    void mainMenuGroup(int g) { mainGroup_ = g; }   // menu2: the main menu's group shown next (0 top, 1 More Modes, 2 Extras)
    std::function<void(int item)> onMainMenu;   // 0 Arcade, 1 PC Mode, 2 Boss Mode, 3 Ranking, 4 Options, 5 Quit
    // ReWrithe: the items that can be chosen (the others grey; user 2026-10-17: PC Mode / Boss Mode locked for now)
    bool mainMenuEnabled[6] = {true, false, false, true, true, true};
    std::function<void()> onCoop;               // Co-op (null: grey)
    // Boss Mode / PC Mode (their own workers): set to start that mode; unset: the item is grey ("coming")
    std::function<void()> startBossMode, startPcMode;
    // The Typing of the Dead 0 (game/typing.h): More Modes' item (null: left out)
    std::function<void()> startTotd;
    std::function<std::string()> sonicValue;    // Sonic Mode's value ("Off" ...; null: the item is left out)
    std::function<void()> onSonic;              // Sonic Mode clicked: the next preset (the item is rebuilt)
    // Survival Mode (game/survival.h; More Modes): "Survival" asks the players and starts the arena shown on the item below it
    // ("Survival Arena: <name>", a click: the next arena). Null: the items are left out
    std::function<std::string()> survivalArenaValue;
    std::function<void()> onSurvivalArena;
    std::function<void()> startSurvival;
    // Horde Mode (game/horde.h; More Modes): "Horde" opens Chapter Select with hordeChapters set (its title says Horde;
    // onChapter reads it), "Horde Size: <name>" (a click: the next size). Null: the items are left out. The main menu
    // clears hordeChapters (the plain Chapter Select is the arcade's)
    bool hordeChapters = false;
    std::function<std::string()> hordeSizeValue;
    std::function<void()> onHordeSize;

    // screen 17 (ours): the Gallery (game/gallery.h; the main menu's "Gallery", grey while openGallery is unset): the
    // hall without the logo (0xD1), "Gallery" at the top, the categories down the left (white 24, the shown one yellow;
    // the last one is the Cut-scenes page, screen 19), then Model (Arcade / PC), Background and Exit; a window
    // (kGalleryRect, 640 x 480 units: the canvas is transparent there) where the host draws the model, under it the info
    // panel (the name in white 24, its place in the category, where it appears, the facts, the description, the
    // motion; 6 x 10), a row of buttons (Previous / Motion / Play or Hold / Next) and the controls line (keyboard or
    // pad). onGallery(action): 0 previous, 1 next, 2 model source, 3 next motion, 4 play / hold, 5 reset the view,
    // 6 exit, 7 next background, 11 full view, 12 info panel, 100 + c category c; the page is rebuilt after each
    // (galleryText gives its texts)
    void screenGallery();
    static constexpr int kGalleryRect[4] = {158, 44, 474, 262};   // x, y, w, h
    // gallery3: the window with the info panel hidden (I): down to the motion line
    static constexpr int kGalleryRectTall[4] = {158, 44, 474, 338};
    // gallery3, the full view (F, pad Start; user 2026-10-21: "like the RE9 gallery model viewer"): the host draws the
    // model over the whole window; the page is only the name and category at the bottom and, in the last band
    // (kGalleryHintY down), the controls as small prompts the host fades out after a few seconds; no hand, no help line
    static constexpr int kGalleryHintY = 452;
    struct GalleryText {
        std::string name, group, text, motion, source, status, where, facts, background;
        int index = 0, count = 0;   // the entry's place in its category
        int category = 0;           // GalleryCategory (game/gallery.h)
        std::vector<std::string> categories;   // their names (the page adds "Cut-scenes")
        bool otherSource = false;   // the other model source can be shown
        bool playing = true;
        bool pad = false;           // the last input was a pad: its controls line
        bool full = false;          // gallery3: the full view
        bool info = true;           // gallery3: the info panel shown (I)
        bool autoTurn = false;      // gallery3: the slow turn (T)
        std::string light;          // gallery3: the light's name (L)
        // gallery3, the Remake layout (optional: launcher.ini gallerylayout=1, key K): the category's entries (their
        // numbers in galleryEntries) and names, the description's page (Tab; the page wraps it)
        int layout = 0;
        std::vector<int> entries;
        int page = 0;
        // regallery: the category's entries' names (as entries), the entry's key (the file panel's cross-fade)
        std::vector<std::string> entryNames;
        std::string key;
        // gallery blood (the Gallery's own blood settings, game/gallery.h GalleryBlood): Blood Color (0-5, the custom
        // colour), Rainbow Blood and its settings, Blood Particles; `blood` the colour's label. onGallery: 40 / 41 Blood
        // Color next / back, 42-47 the custom red / green / blue up / down, 48 Rainbow Blood, 49 its Colours, 50 / 51
        // its Speed, 52 / 53 its Creature Blood, 54 Blood Particles; 34 / 35 the Remake page's panel (Viewer, Blood,
        // Damage)
        int bloodColor = 0, bloodRgb = 0xFFFFFF;
        bool rainbow = false, particles = true;
        std::string rainbowColours, rainbowSpeed, creatureBlood, blood;
        std::string bloodHex() const   // " #RRGGBB" after "Custom" (else empty)
        {
            if (bloodColor != 5 || rainbow) return "";
            std::string s = " #";
            for (int k = 20; k >= 0; k -= 4) s += "0123456789ABCDEF"[(bloodRgb >> k) & 15];
            return s;
        }
        // gallery dismemberment: per body part (Head, Right Arm, Left Arm, Body, Legs) its name, state ("Whole",
        // "Damage 1", "Off", "-") and last state (0: nothing to change); the keys' part; why there is nothing (or "").
        // onGallery: 60 + part its next state, 70 + part back, 59 all whole
        std::vector<std::string> partNames, partStates;
        std::vector<int> partSteps;
        int damagePart = 0;
        std::string damageNote;
        int panel = 0;
    };
    // gallery3: the model's rectangle of the page as it is now (kGalleryRect, or kGalleryRectTall without the info);
    // regallery: the Remake style's Gallery: its stage (units of the wide screen)
    // sweep2 (rtl): the Classic page mirrored (galleryMirror_): the window at the left
    const int* galleryRect() const
    {
        const int* r = galleryRE() && screen_ == 17 ? reFrame_ : galleryInfo_ ? kGalleryRect : kGalleryRectTall;
        if (!galleryMirror_ || (galleryRE() && screen_ == 17))
            return r;
        galleryRectM_[0] = kWidth - r[0] - r[2], galleryRectM_[1] = r[1], galleryRectM_[2] = r[2], galleryRectM_[3] = r[3];
        return galleryRectM_;
    }
    // regallery (gallery_re.cpp, user 2026-10-22 "look like a Resident Evil gallery"): in the Remake style the Gallery
    // (layout 0, its default; K: the portraits' layout, the Classic page) and its Cut-scenes page are Remake screens
    // in the manner of the Resident Evil remakes' galleries (only the layout and feel; our own drawing, the PC files'
    // font): the model on a dark stage the host draws under the page (Gallery::reStage), the categories and the
    // entries in a slim left column, the "file" on the right (the name, a rule, its line, the description, the stats,
    // the weak-point card), the controls as key caps along the bottom; the file and the list cross-fade
    // menus3: the Gallery list's wheel (notches + up) / its scroll bar (start: a press, else a drag) at a page point
    // (units); true: the list took it (build the page again)
    bool galleryListWheel(int ux, int uy, int notches);
    bool galleryRE() const { return sc_ > 1 && (screen_ == 17 || screen_ == 19) && galleryRe_; }
    // the stage's framing for the model (x, y, w, h units) and the panels' edges for its shading: the left column's
    // right edge, the file panel's left edge (cw_ with the info hidden), the controls' top (units)
    const int* galleryReFrame() const { return reFrame_; }
    const int* galleryRePanels() const { return rePanels_; }
    void cue() { remakeCue(); }   // regallery: the menu's move sound (the host's Gallery keys)
    bool galleryFull() const { return screen_ == 17 && galleryFull_; }
    std::function<GalleryText()> galleryText;
    // gallery3, the Remake layout: an entry's portrait (Gallery::kThumbW x kThumbH RGBA, null: not made yet) and the
    // shown entry's weak-point card (4 bytes w, h as u16, then the RGBA; null: none). onGallery: 1000 + n picks entry n
    std::function<const std::vector<uint8_t>*(int entry)> galleryThumb;
    std::function<const std::vector<uint8_t>*()> galleryCard;
    bool hideHand = false;   // gallery3: the hand not drawn (the Remake layout: the mouse between its parts)
    // Light Gun Edition: aim help for light guns in the menus: a shot / hover that lands on nothing takes the nearest
    // button / row within kSnapRange menu units (about one and a half rows), so the menus are easier to shoot
    bool snapAim = false;
    // Light Gun Edition: the controls prompts / hint lines name the gun (Shoot = choose, Reload = back), no keys or pad
    // buttons, no Left / Right tab prompts (the tabs are shot)
    bool gunPrompts = false;
    static constexpr int kSnapRange = 40;
    // galfix (user 2026-10-26: "the bony-hand cursor vanishes in the middle of the screen"): over the Gallery's model the
    // hand is drawn at this opacity instead of hidden (1: as everywhere else)
    float handAlpha = 1.0f;
    // galfix: the hand's size against the PC sprite (90 %, drawn smooth with a soft shadow; both menu styles); its top
    // left (the finger tip) stays at the pointer
    static constexpr float kHandScale = 0.9f;
    bool galleryRemake() const { return screen_ == 17 && galleryRemake_; }
    // remakepages (frontend_remake_pages.cpp): the Gallery (its Classic layout) and its Cut-scenes page in the Remake
    // style keep the PC's 640 x 480 page but over the Remake backdrop across the window (no black side bars): the page
    // is transparent where the hall was; the host draws backdropLayers() (the live attract / hall + the splatter, no
    // widgets) first. backdropPicture: the same on the CPU (--frontend-shot), RGB width x height
    bool classicBackdrop() const
    {
        return style == kStyleRemake && sc_ == 1 && (screen_ == 19 || (screen_ == 17 && !galleryRemake_ && !galleryFull_));
    }
    const RemakeLayers& backdropLayers();
    std::vector<uint8_t> backdropPicture(int width, int height);
    std::function<void(int action)> onGallery;
    std::function<void()> openGallery;   // the main menu's Gallery (null: grey)

    // screen 19 (ours): the Gallery's Cut-scenes page (game/scene_view.h; "Cut-scenes" at the Gallery's top right): the
    // player cut-scenes in two columns (white 24, the chosen one marked), its line, Players (1-4) / Models (Arcade, PC)
    // / Play, Clean View (On / Off), a status line, the viewer's keys, Back (Escape: the Gallery). onScenes(action):
    // 0 players, 1 models, 2 play, 3 back, 4 Clean View, 100 + n choose scene n; the page is rebuilt after each
    // (scenesText gives its texts). actorswap: Actors, a row per role (castRoles / castActors): 10 + r its next actor,
    // 20 + r its previous one (Left / Right in the Remake style, a click: the next)
    void screenScenes();
    struct ScenesText {
        std::vector<std::string> rows;
        std::string line, status;
        int selected = 0, players = 4;
        bool pc = false, pcThere = true, clean = true, subtitles = true;
        std::vector<std::string> castRoles, castActors;
        // galact: the dropdown list of the Actors rows (both styles): every actor's name and each role's pick (its index
        // there); onScenes(-(1000 + 100 r + k)): role r played by actor k
        std::vector<std::string> castChoices;
        std::vector<int> castNow;
    };
    std::function<ScenesText()> scenesText;
    std::function<void(int action)> onScenes;   // null: the Gallery has no Cut-scenes button

    // screen 20 (ours): the Jukebox (game/jukebox.h; the main menu's "Jukebox" after Gallery, grey while openJukebox is
    // unset; frontend_jukebox.cpp): the hall (0xD1), "Jukebox", the categories as tabs (Music / Sound Effects /
    // Voices) and the source (Arcade / PC), ten rows a page (title left, number right; the chosen one yellow, the one
    // playing marked), the page arrows, what plays with its time, a progress bar, the info line, the buttons
    // (Previous / Play / Stop / Next / the mode), the volumes and Exit (Escape: the main menu). onJukebox(action):
    // kJb* below, the page is rebuilt after each; refreshJukebox() rebuilds it when its texts changed (each frame)
    void screenJukebox();
    void refreshJukebox();
    enum : int {
        kJbPrev = 0, kJbPlay, kJbStop, kJbNext, kJbMode, kJbSource, kJbExit, kJbMusicDown, kJbMusicUp, kJbSfxDown,
        kJbSfxUp, kJbPageUp, kJbPageDown, kJbTab = 20 /* + category */, kJbRow = 100 /* + row of the list */,
        kJbSeek = 10000 /* + 0-1000 */
    };
    struct JukeboxText {
        std::vector<std::string> tabs;
        int tab = 0;
        std::vector<std::pair<std::string, std::string>> rows;   // the whole list: title, number
        int selected = 0, playing = -1;
        std::string source;      // "Arcade" / "PC"
        bool otherSource = false;
        std::string now, time, info, mode;
        int progress = -1;       // 0-1000, -1: no bar
        int musicVolume = 100, sfxVolume = 100;
    };
    std::function<JukeboxText()> jukeboxText;
    std::function<void(int action)> onJukebox;
    std::function<void()> openJukebox;   // the main menu's Jukebox (null: grey)

    // screen 9 (ours): Chapter Select: the four chapters as the PC's boss-select portraits (SPR.RBH 0xE3 Chariot, 0xE4
    // Hanged Man, 0xE5 Hermit, 0xE6 Magician; the yellow ones 0xE8.. while hovered), "Chapter N" under each, the ending's
    // line, Exit (the main menu). onChapter(0-3): start that chapter
    void screenChapters();
    bool chapterEnabled[5] = {true, true, true, true, true};   // 0-3 the chapters, 4 the ending (after Chapter 4)
    std::function<void(int chapter)> onChapter;

    // screen 8: High Scores (THOTD.EXE screen_high_scores 0x423038, from the main menu's Ranking 0x423028) UNVERIFIED
    // (teaser): background 0xD2, "High Scores" (0x43, white 32) and the table's name ("Arcade Mode", 0x44), then ten
    // rows in three columns (x 0x40 / 0x100 / 0x1C0, 0x80 wide, from y 0x60, 0x1A apart): the name (left), the stage
    // (0x5CEC74: "Stage 1".."Stage 4"; right) and the score ("%d", right), Exit at y 0x1A0 (0x4210F4: the main menu;
    // Escape). The original's page arrows (0xD9 / 0xD8 -> 0x424E10 / 0x424D64: the boss-mode tables) are left out:
    // the teaser has the arcade table only. The rows are ours (the arcade's top 10; the stage is not kept: "-")
    struct RankRow {
        std::string name, stage;
        int score = 0;
        int ch = -1;   // PC Mode's table: the row's character (0..6 of THOTD.EXE's table 0x5CE348; -1 none)
        bool time = false;   // a Boss Mode times table (the PC's screens 10-14 after 8 / 9): `stage` is the time
    };
    std::function<std::vector<RankRow>()> rankingRows;
    // ours: more tables (Sonic Mode's presets, the 4P table ...): their names; the PC's page arrows (0xD8 / 0xD9, yellow
    // 0xDA / 0xDB) step rankingTable; rankingTableRows(table) gives a table's rows (empty names: the arcade's only)
    std::vector<std::string> rankingTables;
    std::function<std::vector<RankRow>(int table)> rankingTableRows;
    int rankingTable = 0;
    void screenRanking();

    // screen 16: PC Mode, the character select (THOTD.EXE 0x4259AC, from the main menu's PC Mode 0x4259A4; src/game/
    // frontend_pcmode.cpp, docs/pcmode.md) UNVERIFIED (teaser): background 0xA4, two panels (0x420080) with the
    // character's portrait, Hit Points / Chamber / Bullet Damage / Reload Time / Hit Size and the name between two
    // arrows (0x425B5C / 0x425BAC), Start (0x425990: frontend_state 5 -> onPcModeStart) and Exit (the main menu)
    void screenPcMode();
    // 0x5CE4E4: player 1 Rogan, player 2 No Player (0..6, table 0x5CE348); [2] / [3]: ours, players 3 / 4 (4P)
    int pcChars[4] = {1, 0, 0, 0};
    static std::vector<std::string> pcExtraNotes(int ch);
    // pmfix2 (docs/pcmode.md "Extra Characters"): the extras' own portraits (made from their models at first launch,
    // game/pm_portraits.cpp) as sprites 0x700 + ch (the pick screen's, the PC's 0xED..0xF2 size) / 0x740 + ch (the
    // small picture, 0xF3..0xF8's); pcPortraitId: that one when made, else none (pmpick, user 2026-10-30: never the
    // picture of the PC character it plays as); pcPortraitsChanged: the pick / High Scores built again with the new ones
    static constexpr int kPmPortraitBig = 0x700, kPmPortraitSmall = 0x740;
    void setPcPortrait(int ch, bool small, int w, int h, std::vector<uint8_t> rgba);
    int pcPortraitId(int ch, bool small) const;
    void pcPortraitsChanged();
    const assets::PcSprites& sprites() const { return spr_; }
    void pcNameText(Widget* name, int ch, int room);   // pmextra: the name between the arrows (Classic)   // pmextra: an extra character's pick-screen lines (7..19)
    std::function<void()> onPcModeStart;    // Start: the host starts a PC Mode game with pcChars (main.cpp startPcMode)
    // ours (4P, docs/pcmode.md "4 players in PC Mode"): with Players 3 / 4 the screen has four panels, two in each of
    // the background's stones (P1 / P3 left, P2 / P4 right: the original's panels split in a top and a bottom half),
    // each with the portrait at half size, "Player N" in the player's colour frame, the name between the arrows, a
    // Ready button and the five ratings. Every seat picks with its own input (main.cpp routes keys / pads by seat;
    // the mouse can click every panel), a seat with a character must be Ready (computer seats: a random pick, ready),
    // and the game starts as soon as all of them are (no Start item: a status line says who is still picking).
    // pcModeOpen: the screen for that many players (1-2: the original's two panels, unchanged)
    void pcModeOpen(int players, const bool cpu[4], uint32_t seed);
    // a seat's action (kPcPrev / kPcNext the arrows, kPcReady toggles Ready, kPcUnready); the screen is rebuilt, and
    // when every seat that plays is ready onPcModeStart runs. Ignored with 2 panels; the arrows while the seat is Ready
    void pcModeSeat(int seat, int act);
    // Light Gun Edition: a gun's trigger on the 2-panel screen: that player's next character (player 2: No Player too)
    void pcModeGunNext(int seat) { if (screen() == 16 && pcSeats == 2 && seat >= 0 && seat < 2) pcModeStep(seat, 1); }
    // kPcCpu (players 2-4 with a character): the seat a CPU teammate (ready) / back to a person (not ready)
    enum : int { kPcPrev = 0, kPcNext = 1, kPcReady = 2, kPcUnready = 3, kPcCpu = 4 };
    int pcSeats = 2;                        // 2: the PC's two panels; 4: the four panels
    bool pcReady[4] = {}, pcCpu[4] = {};
    bool pcModeAllReady() const;
    // Boss Mode (THOTD.EXE 0x422B90, screen 7; from the main menu's Boss Mode): background 0xA8, "Boss Mode" (0x42, white
    // 32, centred) at the top, the five boss pictures as buttons (0xE3..0xE7, hovered 0xE8..0xEC; flags 0x100060):
    // Chariot top left (16, 16), Hangedman top right (0x270 - w, 16), Hermit bottom left (16, 0x1D0 - h), Magician
    // bottom right, All Bosses in the middle; their handlers 0x422EE4 .. 0x422FE8 start the game (onBossMode: boss mode
    // 2-6); Exit (0x24, y 0x1A0, Escape) -> the main menu (0x422EC8). The host's startBossMode hook opens it (main.cpp's enterMenu).
    void screenBossMode();
    std::function<void(int mode)> onBossMode;
    // the boss times tables (screens 10-14, THOTD.EXE screen_boss_times_1..5 0x423A90 ..): background 0xD2, "High
    // Scores" (0x43), the table's name (0x45 + mode - 2: "Chariot Boss" .. "All Bosses"), ten rows from y 0x60 (0x1A
    // apart): the name (x 0x40, 0xE0 wide, left) and the time (x 0x160, right; "%02d:%02d:%02d"); the page arrows
    // 0xD9 / 0xD8 (hovered 0xDB / 0xDA) at y 0x1A0: right (0x424E10) the next table, left (0x424D64) the last, in the
    // order 8 (Arcade) 9 (PC Mode) 10 .. 14 (ours: the arrows step the five boss tables; the menu's High Scores has its own
    // tables); Exit (Escape) -> the main menu. Our screen ids 30 + mode (10.. are the menu's pages). bossRows(mode):
    // the rows (name, time text in `stage`); highlight: the new row (-1 none)
    void screenBossTimes(int mode, int highlight = -1);
    std::function<std::vector<RankRow>(int mode)> bossRows;
    // screen 15: the name entry (THOTD.EXE screen_15 0x424EA8, from 0x419168 when IsHighScore 0x425748 says the time
    // goes in the table: 0x5CE340 = 3) UNVERIFIED (teaser): background 0xD2, "Player 1" / "Player 2" (0x4D / 0x4E,
    // white 24) at y 0x20, "Congratulations!" (0x4A, white 32) at 0x40, "You have a high score." (0x4B) at 0x70, the
    // score / time (0xA0), "Please enter your name:" (0x4C, white 24) at 0xD0 and the field (0x424F7C) at 0xF0: the
    // name centred in white 24 with the cursor (sprite 0x15A, blinking) before the character at the cursor. The keys
    // (0x425188): letters, digits and space insert (10 at most), Backspace / Delete, Left / Right (keypad 4 / 6),
    // Home / End move the cursor, Enter / keypad Enter / Escape end it (an empty name: "ANONYMOUS", 0x5A14C8).
    // Ours (not in the PC, which takes the keyboard only): a grid of letters under the field (A-Z, 0-9, Space, Del,
    // End) for the mouse, the pads (the pointer moved by the d-pad / stick, South picks, East deletes) and the light
    // guns (a shot picks). onNameEntered(name): the host files the row and opens the table
    void screenNameEntry(const std::string& scoreText, int player = 0);
    std::function<void(const std::string& name)> onNameEntered;
    const std::string& entryName() const { return name_; }

    // frontend_run 0x41DD3C's start (screen state 0): the pointer at (5, 5), cue 1 (samples off), the menu music, the
    // main menu. The PC sends cue 22 (MUSIC_STG4BOSS_3, CD track 12), which is the Chapter 4 song (the arcade's Chapter
    // 4 cue 0xA0100E); ours sends it by default too (menuCue, the "Menu music" setting; cue 21 name entry = the user's patch 9)
    // (C:\Games\HOTD\PATCHES.md: THOTD.EXE 0x41DFC8 / 0x425D45 / 0x425E0A, 0x16 -> 0x15)
    // the "Menu music" choice (launcher.ini menumusic, --menu-music): default cue 22; cue 21 (name entry) is the user's patch 9
    int menuCue = 22;
    void start();
    std::function<void(int cue)> onCue;   // the PC's sound_cue (0x412000) numbers

    // one front-end frame (frontend_run state 13): frontend_input 0x41DA18 -> 0x420A60 (pointer, click, activation)
    // -> 0x42095C (the cursor's click frames) -> the hover walk; nowMs: a millisecond clock (timeGetTime)
    void update(const FrontendInput& in, uint32_t nowMs);
    // the start-up window (THOTD.EXE 0x419FDC, its messages 0 / 0xD; paint 0x419F18) UNVERIFIED (teaser): the SEGA
    // logo (sprite 0xA1) for 3 s or until a key / click, then the title art (0xAA, "(c) Sega Enterprises, Ltd., 1996,
    // 1998") for 5 s (a key / click: at once); update() runs it instead of the menus while splashActive()
    // logoOnly (liveattract: the Remake style's boot): the SEGA logo only, then the attract
    void splashStart(uint32_t nowMs, bool skipLogo = false, bool logoOnly = false);
    bool splashActive() const { return splash_ >= 0; }
    int splashStage() const { return splash_; }   // (testing: 0 the SEGA logo, 1 the title art, -1 none)
    // the draw (0x420904): widgets, then the cursor, into a kWidth x kHeight RGBA canvas
    void render(std::vector<uint8_t>& canvas);

    // screen 2: Options (THOTD.EXE 0x41E428, from the main menu's Options 0x421434). The values are ours
    // (LaunchConfig, through `options` / onOptions); the original's rows and places:
    //   Difficulty  (0x421520; ours: Very Easy .. Very Hard, the five operator levels)
    //   Life 1P / 2P (0x421590 / 0x4215BC; ours: one setting for both: the starting lives, cycling the operator's
    //               lives table), Continues (0x421564: 0-9; PC Mode only, arcade games are free play), Auto Reload 1P / 2P
    //               (0x4215E8 / 0x421614; ours: one switch), Blood Color (0x4214E0: Red, Green, Blue, Purple, Yellow),
    //   Sound Effects / Music sliders (0x426E80 draw, 0x426D5C hit, 0x426DC0 click: 8 steps; ours 0-100 %),
    //   Resolution (ours: the Video page's, resolutionValue / onResolution), Controller Config (0x421484 -> our text launcher for now), Exit
    //   (0x421450: back to the main menu; Escape)
    void screenOptions();
    struct Options {
        int difficulty = 2, lives = 9, blood = 0, sfxVolume = 100, musicVolume = 100;
        bool autoReload = false;
        int continues = 5;   // PC Mode's Continues 0-9 (THOTD.EXE 0xD5B1E0; its handler 0x421564: +1, past 9 -> 0)
    } options;
    // the Remake menu's blood (its splatter, the panel's red bottom, the handprints, the focus bars) in the Blood Color
    // setting's colour: -1 from options.blood (red, green, blue, purple, yellow), else 0xRRGGBB (the custom colour / a
    // texture pack's [recolour] blood)
    int bloodRgb = -1;
    // blood2: the splatter's pattern (launcher.ini menublood; 0 the first one, the same for everyone). A click on the
    // backdrop (outside the panel and the buttons), a pad's right stick (R3) or the B key: newSplatter() (a new one,
    // cross-faded); onSplatSeed: save it
    uint32_t splatSeed = 0;
    // the Gallery's Cut-scenes page > Attract Demo (liveattract; both styles): the host starts the attract (as it is, full
    // screen with sound), then clears it; any key / button comes back to that page
    bool attractAsked = false;
    float menuFade() const { return menuFade_; }   // blood2: the Remake main menu's idle fade (0 shown .. 1 gone)
    std::function<void(uint32_t)> onSplatSeed;
    void newSplatter();
    // the splatter alone, whole (cw units wide, ps pixels a unit, RGBA straight alpha): the GPU menu's layer (thread-safe)
    static std::vector<uint8_t> splatterLayer(int cw, float ps, uint32_t seed, uint32_t hue);
    std::function<void()> onOptions;      // a value changed (save it, set the volumes)
    std::function<void()> onControllerConfig;
    // Resolution (ours, user 2026-10-21): its value and its page (Video > Resolution); null: grey "Auto"
    std::function<std::string()> resolutionValue;
    std::function<void()> onResolution;
    // optui: the crosshair preview's game picture: PNG files to try in order (the live attract's saved stills); none
    // readable: the hall (0xD1) in its own colours
    std::vector<std::string> previewStills;

    // ReWrithe's own page (menuart step 3; not in THOTD.EXE): built the Options screen's way (its background, title,
    // rows of white 24 labels with right-aligned values, Exit at the bottom); reached from Options' extra item
    // "More Options" between Controller Config and Exit. The rows come from the host (our settings).
    struct Extra {
        std::string name;
        std::function<std::string()> value;   // the value text (empty: an action row)
        std::function<void()> click;          // null: shown grey
        std::string help;                     // the one-line explanation at the bottom (the 6 x 10 PC font)
        // ours (uxpolish): why the row does nothing right now (non-empty: the row is grey, not clickable, and this is
        // its help line), and whether it is an experimental feature (a small EXPERIMENTAL tag after the label, the help
        // line starts with "Experimental:"). Both null: an ordinary row
        std::function<std::string()> off;
        std::function<bool()> experimental;
        std::function<void()> clickBack;   // remakemenu: Left on the row (the value before; null: click, the next)
        // remakefix (user 2026-10-22): a choice's values in order (value() is one of them): the Remake style opens a
        // dropdown list on a click / Enter / A (Left / Right still step; optui: any length, a long list scrolls), the
        // Classic style on a right-click (a click still steps); empty: the click steps
        std::vector<std::string> choices;
        std::function<void(const std::string&)> pick;   // remakefix: sets that value at once (null: the click steps to it)
        bool raw = false;   // lang: its name is translated already (a line of a wrapped message)
        // optui: the current value's index in choices (-1: none of them; null: the one equal to value()), and setting
        // choices[k] at once (null: pick / the click's steps)
        std::function<int()> choiceNow;
        std::function<void(int)> pickAt;
        // padfix: choice k is not available now (grey in the list, not picked; Button Style without icons for the pad)
        std::function<bool(int)> choiceOff;
        // optui (user 2026-10-26 "show the crosshair options so players know what it looks like"): the crosshair rows:
        // the sight's picture for choice k (-1: the settings now) of seat 0-3 in a window wh pixels high. The page draws
        // a live preview beside these rows (every player's crosshair on a picture of the game) and the dropdown's
        // values as their icons
        std::function<bool(int choice, int seat, int wh, SightArt&)> art;
        // menufb (user 2026-10-26: "the music and sfx volume should have a bar"): the value as a bar (0 .. barMax in
        // steps of barStep) beside its text, clicked / dragged with the mouse (both styles); null: none
        std::function<int()> barGet;
        std::function<void(int)> barSet;
        int barMax = 100, barStep = 10;
        std::function<uint32_t()> swatch;   // menufb: a colour square left of the value (0xRRGGBB; the custom blood colour)
        // fix28 (user 2026-10-27: "some settings show you comparison images"): the game's own picture of choice k (a PNG
        // the game rendered of a fixed Chapter 1 frame with that value; "" not made yet / none). The Remake pages show
        // them beside the focused row (up to 3 choices side by side, the current one marked; more: the current one)
        std::function<std::string(int choice)> preview;
        // fxux: the choices whose pictures show side by side (empty: up to 3 of them, else the current one); a current
        // choice not among them marks the first (Transparency's Auto: the Arcade Dots picture)
        std::vector<int> previewList;
        // relfix2 (user 2026-11-03: "the differences aren't noticeable"): what to look for in choice k's picture
        // (non-empty: the pictures shown as one that flips between the choices each second and a close-up of each, the
        // same spot where they differ most, 4x with nearest pixels, each with its look-for line)
        std::vector<std::string> previewLook;
    };
    // fix28: the preview pictures' maker's progress (a new number: a picture finished, the pages draw them again)
    std::function<int()> previewGen;
    // menus3: a picture made in memory (Extra::preview gave "mem:<key>"): its RGBA; false: not ready
    std::function<bool(const std::string& key, std::vector<uint8_t>& rgba, int& w, int& h)> previewImage;
    std::vector<Extra> extras;
    // fxux (user 2026-11-03: Settings in the pause menu): the in-game Settings' change to a settings page's rows before
    // it is built (menu_pages.h menuInGameRows; title: the page's) and the Classic Options screen's note on its game
    // rules (Difficulty, Lives, Continues, Blood Color: from the next game); null / empty: the menus'
    std::function<void(std::vector<Extra>& rows, const std::string& title)> rowsFilter;
    std::string inGameNote;
    // menufb (user 2026-10-26: "clicking crosshairs takes them to a new page / firing range"): the extras page shows the
    // Firing Range under its rows (the rows: menu_pages.h menuRangeRows; both styles): targets to shoot, every player's
    // crosshair where that player aims, hits / shots per player. rangeArt: player seat's crosshair as the game draws
    // it in a window wh pixels high; rangeSig: the crosshair settings' signature (the pictures made again when it
    // changes); artSeat: the player whose crosshairs the dropdowns' icons show
    static constexpr const char* kRangeTitle = "Firing Range";   // (the extras page with this title shows the range)
    bool extrasRange() const { return extrasTitle == kRangeTitle; }
    std::function<bool(int seat, int wh, SightArt&)> rangeArt;
    std::function<std::string()> rangeSig;
    int artSeat = 0;
    // the host's aims on the range, each frame before update (menu units; on: that player aims now (a pad, a gun);
    // shot: its trigger this frame). The mouse (the pointer) aims for rangeMouseSeat, read by the Frontend itself
    struct RangeAim { bool on = false; float x = 0, y = 0; bool shot = false; };
    RangeAim rangeAim[4];
    RangeAim rAimNow_[4];   // (the aims shown this frame: the host's and the mouse's, inside the range)
    int rangeMouseSeat = 0;
    int rangeFlash = 0;   // the host's Teammate Crosshair Flashes (LaunchConfig::teamFlash): the range's other players show it
    // padpass: the keyboard players aim on the range (Tab on the page: their keys aim / shoot there, not the menu's;
    // Tab / Esc again: the menu's keys); the host reads the keys, the page shows the state
    bool rangeKeys = false;
    // padpass: an Xbox-style prompt key ("A", "LB/RB", "Start/B") as the last pad's type labels it ("Cross", "L1/R1")
    std::string padKey(const std::string& xbox) const;
    int padType() const { return padType_; }
    bool padPromptsShown() const { return padPrompts_; }
    void showPadPrompts(int type) { padPrompts_ = padForced_ = true, padType_ = type; }   // (--frontend-shot: HOTD_FE_PAD)
    bool rangeShown() const { return rangeOn_ && root_ != nullptr; }
    void rangeReset();   // the targets and the scores anew (the page opened)
    int rangeHits(int seat) const { return rHits_[seat & 3]; }     // (testing)
    int rangeShots(int seat) const { return rShots_[seat & 3]; }
    void rangeTargetAt(int k, float& x, float& y) const;          // (testing: target k's centre, units)
    std::string extrasTitle = "More Options";   // the page's title (ours: "Steam" for the Steam pictures)
    std::function<void()> extrasExit;            // Exit / Escape; null: back to Options
    void screenExtras();
    // rows a page (title at 0x14, the tab strip at 0x3A, rows from 0x5C every 0x1A, Exit at 0x19A with the PC's page
    // arrows 0xD8 / 0xD9 beside it, the help lines from 0x1BC: all inside 640 x 480)
    static constexpr int kExtrasPerPage = 12;
    static constexpr int kExtrasWidth = 0x1C0;   // a row's width (label left, value right), 640 x 480 units
    // a row text's width in the rows' font (white 24); 0 when the PC art is not loaded (the text launcher)
    int rowTextWidth(const std::string& s) const
    {
        int w = 0, h = 0;
        measure(s, 0x20000, w, h);
        return h > 0 ? w : 0;
    }
    int rowTextWidthRaw(const std::string& s) const   // lang: of a text translated already
    {
        int w = 0, h = 0;
        measureRaw(s, 0x20000, w, h);
        return h > 0 ? w : 0;
    }
    int extrasPage = 0;                        // reset when extrasTitle changes
    // the settings' categories (Video, Audio, ...): a strip of tabs under the title of a category's page (extrasTab: its
    // index; -1 = no strip: a sub-page such as Steam or a Controller page) and on the Options screen (where "More
    // Options" was); onTab(i) opens category i
    std::vector<std::string> tabs;
    std::function<void(int)> onTab;
    int extrasTab = -1;

    // the pointer (0xD5A97C / 0xD5A980)
    void setPointer(int x, int y) { px_ = x; py_ = y; }
    int pointerX() const { return px_; }   // padglyphs (the pad cursor: menu_cursor.h)
    int pointerY() const { return py_; }
    // one front-end frame without input: the hover walk (0x427938), then the draw (0x420904): widgets + cursor
    void frame(std::vector<uint8_t>& canvas);
    // activates the widget under the pointer (0x4277B0)
    void activate();

    int screen() const { return screen_; }

    // ---- the PC's secret codes (THOTD.EXE frontend_input 0x41DA18; docs/teaser_status/pc_cheats.md; tip: RUNESA) ----
    // Ctrl held + SKIDMARX toggles the debug flag 0x5CD374 (main menu: Cheat, Edit Char Stats under Quit; 10 s idle
    // to the attract; in game: Ctrl + 1 / 2 infinite lives and rounds for P1 / P2, the "fps / scores" line); Ctrl held +
    // CREATURE toggles 0x5CE898 (Creature Test under them, shown with the debug flag only). Not saved (run only).
    bool cheatsOn() const { return debugItems_; }
    // screen 6: Cheat (THOTD.EXE 0x4228CC): Stage (0x422790 next, wraps), Scene (0x4227CC next, wraps), Free-Play
    // (0x4228C0, 0xD5B35C), Start (0x420F70: the game from that stage / scene), Exit (0x4210F4)
    void screenCheat();
    std::function<void(int stage, int scene, bool freePlay)> onCheatStart;
    // Free-Play (0xD5B35C): the PC keeps it in thotd.cfg (config_save 0x422194 writes 0xD5B1D4..0xD5B35F) and every
    // game's pc_continues_init 0x4488E4 reads it: PC Mode's continues never run out. Ours: launcher.ini freeplay=, set
    // by the runtime; onFreePlay saves a change
    bool freePlay = false;
    std::function<void(bool)> onFreePlay;
    // Edit Char Stats (0x426188 -> screen_pc_mode_edit 0x426190): ours opens the PC Mode screen (null: grey)
    std::function<void()> onEditCharStats;
    // screen 5: Creature Test (THOTD.EXE screen_creature_test 0x422324): the body object's name (table 0x5F10E0, 94),
    // next / last BO (0x4222AC / 0x4222DC), the motion's name (next / last MO do nothing in the original), Left / Right
    // (0x422274 / 0x422290: the view's yaw +-0xB4), Exit (0x42223C)
    void screenCreatureTest();
    // ours: "View" opens the Gallery (game/gallery.h) at that creature type (BO index = creature type); null or false: grey
    std::function<bool(int bo)> creatureViewable;
    std::function<void(int bo)> onCreatureView;
    int creatureBo() const { return creatureBo_; }
    int creatureYaw() const { return creatureYaw_; }
    // frontend_state 6 (0x420F2B): idle on the main menu for 30 s -> the attract; the host leaves the menu and clears it
    bool attractDue() const { return attractDue_; }
    void clearAttract() { attractDue_ = false; idleReset_ = true; }

private:
    // ---- remakemenu: the menu model (both styles) and the Remake style's layout (frontend_remake.cpp) ----
    struct MainItem {
        int label = -1;          // a string-table id, or -1: text
        std::string text;
        std::string help;
        std::function<void()> act;   // null: grey
        bool heading = false;    // menu2: a group's name (not a button)
        bool debug = false;      // the SKIDMARX items (Cheat, Edit Char Stats, Creature Test) under Quit
        std::string tag;         // polish: a small red line with the label (Survival's "Very Experimental / Proof of Concept")
        std::string name() const { return label >= 0 ? Frontend::label(label) : text; }
    };
    static constexpr const char* kSurvivalTag = "Very Experimental / Proof of Concept";   // (polish, user 2026-11-03)
    std::vector<MainItem> mainMenuItems();   // the main menu's items for mainGroup_ (and the debug items)
    // Options (screen 2): its rows' values and steps (which: kOpt*; dir +1 the next value, -1 the one before)
    enum : int { kOptDifficulty, kOptLives, kOptContinues, kOptAutoReload, kOptBlood, kOptSfx, kOptMusic, kOptRows,
                 kOptMaxLives = 100 };   // (GitHub #30: Max Lives, a row of the Remake tab and Gameplay, not screen 2's)
    void optionsStep(int which, int dir);
    std::string optionsValue(int which) const;
    std::vector<Extra> optionsRows();        // the Remake style's "Game" tab: Options' rows as settings rows
    std::vector<Extra> optRows_;
    int cw_ = kWidth;        // the screen's width in menu units (640; a Remake screen: wideW_)
    int sc_ = 1;             // pixels a unit (a Remake screen: 2, its flag; its pixels: ps_)
    float ps_ = 2.0f;        // blood2: a Remake screen's pixels a unit (setOutputHeight)
    int rpw() const { return int(std::lround(float(cw_) * ps_)); }
    int rph() const { return int(std::lround(float(kHeight) * ps_)); }
    RemakeLayers layers_;
    uint64_t fgGen_ = 0, staticGen_ = 0;
    int staticDirty_[4] = {0, 0, 0, 0};   // leftovers: the static layer's part redrawn last (RemakeLayers::fgDirty)
    float menuFade_ = 0.0f;
    float fadeLin_ = 0.0f;       // remakefix: the idle fade before its easing (0 .. 1)
    uint32_t fadeLastMs_ = 0;    // remakefix: the last update (the fade's step)
    uint32_t lastInputMs_ = 0;
    bool fadeReset_ = true;
    std::vector<RemakeLayers::Part> hov_;   // the GPU menu: the focus / hover / help parts
    // menuperf: the parts drawn on this page so far (a widget lit, a help line), kept while the static layer stays: a
    // focus coming back costs a copy, not a draw
    std::unordered_map<std::string, RemakeLayers::Part> partCache_;
    std::string partCacheSig_;
    size_t partCacheBytes_ = 0;
    // menuperf: the Remake Jukebox's live widgets (its time, its bar) and its texts other than those (refreshJukebox:
    // only the time / progress changed: these updated in place, no new page)
    Widget* jbTime_ = nullptr;
    Widget* jbBar_ = nullptr;
    std::string jbFixedSig_;
    std::vector<std::pair<Widget*, int>> jbRows_;   // the list's rows on the page and their places in the list
    static std::string jukeboxFixedSig(const JukeboxText& t);
    static std::string jukeboxSigOf(const JukeboxText& t);
    bool jukeboxInPlace(const JukeboxText& t);
    std::vector<uint8_t> handImg_;   // the hand's sprite
    int handImgId_ = -1;
    uint32_t bgHue_ = 0;
    void backdropTint(bool on, float mul[3], float add[3]) const;   // the settings pages' tint in the blood colour
    int wideW_ = 854;        // setViewAspect's width
    void blit(int id, int x, int y);   // a sprite at (x, y) menu units (scaled by sc_)
    std::function<void()> rebuild_;    // the Remake screen shown (setViewAspect / setStyle rebuild it)
    // a Remake screen's start: the tree, the wide canvas; key: which page (another key: a new page, the focus on its
    // first item while the keyboard / a pad is used, the rows from the top)
    void remakeBegin(int screen, const std::string& key);
    int rPanel_[4] = {};               // the torn panel (x, y, w, h units)
    int rHelp_[3] = {};                // the help line's place (centre x, y, width)
    int rTabCur_ = -1;                 // the strip's current tab (-1: no strip)
    std::function<bool(int)> rScroll_; // a settings page's rows scrolled by dir (false: at the end)
    void remakeMainMenu();
    void remakeQuit();
    void remakePlayers();              // playerpick: screen 22's Remake layout
    std::string pickMode_;             // playerpick: the game the choice is for ("Arcade", "Chapter 2" ...)
    std::function<void()> pickGo_, pickBack_;
    int pickN_ = 1, pickBot_[4] = {}, pickLevel_[4] = {2, 2, 2, 2};   // the choice; each seat's CPU level when CPU
    bool pickNew_ = false;             // the screen just opened: the hand / focus onto Start
    std::string pickSeatText(int p) const;   // "Player 2: Human" / "Player 2: CPU Normal"
    // a settings page: the tabs (current: its index in the strip, -1 none: `title` on a single tab), the rows, Back
    void remakeSettings(const std::string& title, std::vector<Extra>* rows, int current, std::function<void()> back);
    void remakeTab(int i);             // a tab of the strip ("Game" = Options, then the categories)
    void remakePrompts(Widget* root, bool tabsKeys, std::function<void()> back);
    void remakeNav(FrontendInput& in, uint32_t nowMs);   // Up / Down focus, Left / Right values, Q / E tabs (remakefix: a click
                                                          // closing the dropdown is used up: in.press cleared)
    // remakefix: the settings row whose dropdown is open (its index in the rows; -1 none) on the page ddKey_, the list's
    // rectangle (units), the focus onto its current value at the next build (opened by a key)
    int ddRow_ = -1;
    std::string ddKey_;
    int ddRect_[4] = {};
    bool ddFocus_ = false;
    bool ddOpen() const { return ddRow_ >= 0 && sc_ > 1 && ddKey_ == remakeKey_; }
    void ddClose();
    // optui: the open list's rows (its row's art: the values' icons) and its first value shown (a long list scrolls)
    std::vector<Extra>* ddRows_ = nullptr;
    int ddScroll_ = 0;
    // optui: the Classic style's dropdown (a right-click on a row of screen 100): the row (-1 none) on the page cdKey_,
    // its first value shown, the rows' rectangles on the page (640 x 480) with their index in extras
    int cdRow_ = -1, cdScroll_ = 0;
    // galact: the Classic Cut-scenes page's Actors dropdown: the role whose list is open (-1 none), its first actor shown
    int castCd_ = -1, castCdScroll_ = 0;
    int reListHide_[4] = {};   // galact: the Remake Cut-scenes list's rows under the open Actors list (not drawn)
    std::string cdKey_;
    // menufb (user 2026-10-26: "no indication that the list is scrollable, add a scroll bar to the right side"): a bar
    // beside every list longer than its room (a settings page's rows, a dropdown; both styles): its place (units), the
    // first item shown, how many show, of how many, and the scroll to item `first`. A click on the track pages there,
    // the thumb drags (the wheel and the arrows as before)
    struct ScrollBar { int x = 0, y = 0, w = 0, h = 0, first = 0, vis = 0, total = 0; std::function<void(int)> set; };
    std::vector<ScrollBar> sbars_;
    int sbDrag_ = -1, sbGrab_ = 0;
    void scrollBar(Widget* parent, int x, int y, int w, int h, int first, int vis, int total, std::function<void(int)> set,
                   bool append = false);   // (append: drawn after the parent's other children)
    bool scrollBarInput(const FrontendInput& in);   // true: the input was the bar's (used up)
    static void sbThumb(const ScrollBar& b, int& ty, int& th);
    void drawRScroll(Widget& w);   // (w.user: the bar's index in sbars_)
    void drawCScroll(Widget& w);
    // menufb: the value bars (Extra::barGet): their place (units), value, range, step and the setter (the page again)
    struct SliderBar { int x = 0, y = 0, w = 0, h = 0, value = 0, max = 100, step = 1; std::function<void(int)> set; };
    std::vector<SliderBar> slbars_;
    int slDrag_ = -1;
    void sliderBar(Widget* parent, int x, int y, int w, int h, int value, int max, int step, std::function<void(int)> set);
    void drawRSlider(Widget& w);   // (w.user: its index in slbars_)
    void drawCSlider(Widget& w);
    void drawSwatch(Widget& w);   // menufb: a colour square (w.tint), both styles
    // fix28: the comparison pictures' box (Extra::preview) of row w.user, shown while that row has the focus
    // (rPreviewRow_: the preview widget -> its row's widget); the pictures read once per file and maker's number
    void drawRPreview(Widget& w);
    std::map<const Widget*, const Widget*> rPreviewRow_;
    std::vector<Extra>* rPreviewRows_ = nullptr;
    // (relfix2: zoom: the close-ups' spot, by the other pictures' names and the crop's size: where they differ most)
    struct PreviewPic { std::vector<uint8_t> rgba; int w = 0, h = 0; std::map<std::string, std::pair<int, int>> zoom; };
    std::map<std::string, PreviewPic> previewPics_;
    int previewGenSeen_ = -1;
    uint32_t previewPollMs_ = 0;
    // menufb: the Firing Range: the targets (where in the range 0..1, hit when / by whom / its points), the holes, the
    // players' hits / shots / points, the range's rectangle (units), its widgets in the tree built last
    struct RangeTarget { float u = 0.5f, v = 0.5f; uint32_t hitMs = 0; int hitBy = -1, points = 0; };
    static constexpr int kRangeTargets = 4;
    RangeTarget rTargets_[kRangeTargets];
    struct RangeHole { float u = 0, v = 0; int seat = 0; };
    std::vector<RangeHole> rHoles_;
    int rHits_[4] = {}, rShots_[4] = {}, rPoints_[4] = {};
    int rRect_[4] = {};
    bool rangeOn_ = false, rangeHand_ = false, rTargetsSet_ = false;
    uint32_t rRng_ = 12345, rNowMs_ = 0;
    uint32_t rFlashMs_[4] = {};   // a player's last shot (its flash at the aim)
    Widget* rangeBg_ = nullptr;
    Widget* rangeTw_[kRangeTargets] = {};
    Widget* rangeXw_[4] = {};
    std::map<std::string, SightArt> rangeArt_;
    void rangeWidgets(Widget* parent, int x, int y, int w, int h);
    void rangeTick(FrontendInput& in, uint32_t nowMs);   // the aims, the shots (the mouse's press used up), the targets
    void rangeShot(int seat, float x, float y);
    void rangePlace(int k);                              // target k somewhere free
    void rangeLayout();                                  // the widgets onto the state (positions, signatures)
    const SightArt* rangeSight(int seat, int wh);
    void drawRangeBg(Widget& w);       // the range: its ground, frame, holes and the scores
    void drawRangeTarget(Widget& w);   // w.user: the target
    void drawRangeSight(Widget& w);    // w.user: the player
    void rangePut(int px, int py, uint32_t rgb, float a);   // a canvas pixel blended (either style)
    struct RowRect { int x, y, w, h, i; };
    std::vector<RowRect> cRows_;
    bool cdOpen() const { return cdRow_ >= 0 && sc_ == 1 && screen_ == 100 && cdKey_ == extrasTitle; }
    void cdClose();
    void pickChoice(std::vector<Extra>& rows, int i, int k);   // optui: a list's value k picked (both styles)
    // optui: the crosshair rows' pictures (the dropdowns' icons): the rows of the page shown (their art), the page's
    // art rows' values (the pictures' key), the sights' pictures kept by key; the sample game picture (the Firing
    // Range's ground: previewStills' first that reads, else the hall 0xD1 in its own colours)
    std::vector<Extra>* artRows_ = nullptr;
    std::string artSig_;
    std::map<std::string, SightArt> artCache_;
    const SightArt* rowArt(int row, int choice, int seat, int wh);
    std::vector<uint8_t> prevBg_;
    int prevBgW_ = 0, prevBgH_ = 0;
    bool prevBgTried_ = false;
    void loadPreviewBg();
    void drawCBox(Widget& w);         // optui: the Classic dropdown's panel
    void drawCItemIcon(Widget& w);    // optui: a Classic dropdown value's icon (w.id: its choice; w.user: the row)
    // a sight's picture into the canvas: the Remake style at (cx, cy) units (ps_ pixels a unit), the Classic at (cx, cy)
    // canvas pixels; fit > 0: scaled down to fit a box that size (units / pixels)
    void rSightArt(const SightArt& a, float cx, float cy, float fit);
    void cSightArt(const SightArt& a, int cx, int cy, int fit);
    void remakeCue() { ++remakeCues; if (onCue) onCue(28); }   // remakefix: the menu's sound (the PC's click, GAMESNDS 13) for a move / tab / back
    void remakeFocus(int dir);         // the next / previous nav widget under the pointer
    bool remakeFirstFocus_ = false;    // a new screen: the first nav widget focused (keyboard / pad)
    std::string remakeKey_;            // the screen last built (a rebuild of the same one keeps the pointer)
    int remakeScroll_ = 0;             // a settings page's first row shown
    int remakeTabs_ = 0;               // the strip's tabs (Game + the categories)
    bool rTabsRtl_ = false;            // rtl: the strip laid out from the right (Q / E and Left / Right swapped)
    bool handShown_ = false;           // the Remake style: the bony hand while the mouse is used
    bool padPrompts_ = false;          // the prompts show a pad's buttons
    // padpass: the last pad's type (SDL_GamepadType: the prompts name its buttons: A / B / LB on an Xbox pad, the
    // shapes / L1 on a PlayStation pad, B / A / L on a Nintendo pad); 0: Xbox-style
    int padType_ = 0;
    bool padForced_ = false;   // (--frontend-shot: HOTD_FE_PAD: kept whatever the shot's pointer does)
    // padpass: a PlayStation face button's shape (key "Cross" / "Circle" / "Square" / "Triangle") inside a ring centred
    // at (cx, cy), r units; false: not one (the key's text is drawn instead)
    bool rPadSymbol(const std::string& key, float cx, float cy, float r);
    // padglyphs: a pad button's icon (pad_prompts.h padIcon: Kenney's Input Prompts in data/ui/prompts, the 128 px one
    // past 64 screen pixels), a 2r-unit square a part centred at (cx, cy) ("L1/R1": two side by side). false: no icon for
    // a part (the caller draws the cap, then rPadSymbol's shapes / arrows or the text). A new icon set = new files
    bool rPadGlyph(const std::string& key, float cx, float cy, float r);
    std::vector<std::string> padIconsOf(const std::string& key) const;   // each part's icon ("L1/R1"), none: empty
    bool ringKey(const std::string& key) const;   // a pad key drawn in a ring (one letter / a shape)
    bool navHeld_[4] = {};
    uint32_t navNext_[4] = {};
    uint32_t nowMs_ = 0;               // update()'s clock (the background's slow pan)
    // the drawing (frontend_remake.cpp)
    void remakeRender();
    bool rBackground();   // into bgLine_ (false: unchanged)
    // the blood splatter at the corners and down the sides (blood2: generated per width, drawn sharp at the canvas
    // size): the part inside canvas pixels [cx0, cx1) x [cy0, cy1)
    void rSplatter(int cx0, int cy0, int cx1, int cy1);
    uint32_t bloodHue_ = 0xA00C08;       // this frame's blood colour at the red's level (0xA00C08: red, unchanged)
    uint32_t tint(uint32_t c) const;     // one of the menu's reds in the blood colour
    uint32_t paperInk(uint32_t c) const;   // blood2: tint(c) for letters on the paper (a light colour darkened: readable)
    uint32_t bloodColour() const;        // bloodRgb / options.blood as a colour at the red's level
    void rBlend(int x, int y, uint32_t rgb, float a);   // a pixel (canvas pixels) blended over
    // grungy small caps (align 0 left, 1 centre, 2 right; x, y units, y = top); fx: kTxOutline a dark outline (about
    // 1.5 pixels at 1080 lines), kTxShadow a soft drop shadow
    enum : int { kTxOutline = 1, kTxShadow = 2 };
    void rText(const std::string& s, float x, float y, float h, uint32_t rgb, int align, float distress = 0.0f,
               int seed = 0, int fx = 0);
    float rTextWidth(const std::string& s, float h) const;      // (of the translation)
    float rTextWidthRaw(const std::string& s, float h) const;   // lang: of a text already translated
    void rTorn(float x, float y, float w, float h, int seed, bool glow, float alpha = 0.94f);   // a dark torn panel
    void rPaper(float x, float y, float w, float h, int seed, float tilt, int tone);   // a parchment strip (tone 0 lit 1 normal 2 grey)
    void rHand(float cx, float cy, float s, bool on);             // the bloody handprint (a switch's On / Off)
    void rChevron(float cx, float cy, float s, uint32_t rgb, float alpha = 1.0f);
    void rKeyCap(float x, float y, float w, float h, bool ring);  // a prompt's key: a dark cap with a crisp light border
    // remake2: the blood: the arcade's hit marks (k 0..5 = sprites 0x28..0x2D: slash, bite, claws, stab, splat, smear),
    // as alpha + shade, drawn in a blood red ramp; rBlood: centred at (cx, cy) units, hgt units high, turned by ang
    // radians, mirrored (flip), squashed across (sx: the width's scale)
    struct BloodArt { int w = 0, h = 0; std::vector<uint8_t> a, l; };
    BloodArt bloodArt_[6];
    bool bloodTried_ = false, bloodArcade_ = false;
    std::string regionsDir_;
    void loadBloodArt();
    void rBlood(int k, float cx, float cy, float hgt, float ang, float alpha, bool flip = false, float sx = 1.0f,
                float bright = 1.0f);
    void rImage(const std::vector<uint8_t>& img, int iw, int ih, float x, float y, float w, float h, float alpha);
    void rImageSoft(const std::vector<uint8_t>& img, int iw, int ih, float x, float y, float w, float h);   // logo2: real alpha
    void drawRStrip(Widget& w);       // the main menu's items
    void drawRTab(Widget& w);
    void drawRRow(Widget& w);
    void drawRPrompt(Widget& w);
    void drawRPanel(Widget& w);       // the root: background + torn panel (w.user: 0 main menu, 1 settings, 2 question)
    void drawRLabel(Widget& w);       // a heading / title / help text (w.user: colour, w.h: size)
    void drawRArrow(Widget& w);       // the settings rows' scroll arrows (w.user: 0 up, 1 down)
    void drawRDrop(Widget& w);        // remakefix: a dropdown's panel
    void drawRDropItem(Widget& w);    // remakefix: a dropdown's value (w.user 1: the current one)
    mutable std::map<int, std::vector<uint8_t>> glyphCache_;   // the font's glyphs as images
    const std::vector<uint8_t>& glyphImage(int g) const;
    std::vector<uint8_t> logo_, hall_;   // the logo alone (0xD4), the hall (0xD1): the Remake screens' art
    int logoW_ = 0, logoH_ = 0;
    // logo2: the "ReWrithe" wordmark on the logo (our own art: data\ui\rewrithe_wordmark.png, 3x; wordmarkFile is set
    // by main.cpp; missing: nothing drawn), loaded with the logo
    std::vector<uint8_t> word_;
    int wordW_ = 0, wordH_ = 0;

public:
    static std::string wordmarkFile;
    static std::string promptIconDir;   // padglyphs: data/ui/prompts (main.cpp; empty / missing files: drawn glyphs)
    // padglyphs: Options > Controls > Button Prompts (LaunchConfig::padPrompts: 0 Auto, 1 Xbox, 2 PlayStation,
    // 3 Nintendo, 4 Steam, 5 Keyboard); the host sets it every frame
    int promptChoice = 0;
    int buttonStyle = 0;   // padglyphs: Controls > Button Style (pad_prompts.h PromptPack: 0 Xelu, 1 Kenney, 2 Simple)
    bool iconOutline = false;   // menus3: Controls > Icon Outline: Xelu's icons with the light rim (prerel's), default Off

private:
    // the frame's layers (each redrawn only when it changes): the widgets without the hover (staticSig_: the tree), the
    // foreground (that + the widgets under the hand / focus + the help line; fgSig_), the background (bgKey_: the
    // footage's frame / the hall's drift step) and their composite (comp_), copied to the canvas with the hand over it
    std::vector<uint8_t> fgStatic_, fg_, comp_, bgLine_;
    std::string staticSig_, fgSig_;
    int64_t bgKey_ = -1;
    unsigned treeGen_ = 0;               // remakeBegin's count (a new tree)
    std::vector<uint8_t> splat_;         // the blood at the edges (alpha, shade) and the vignette, a unit each (per width)
    int splatW_ = 0;
    // rBlend's target: the canvas, or a tile (rCached) placed at (rOffX_, rOffY_) pixels, rW_ x rH_
    int rOffX_ = 0, rOffY_ = 0, rW_ = 0, rH_ = 0;
    struct Tile { int x0 = 0, y0 = 0, w = 0, h = 0; std::vector<uint8_t> rgba; };
    std::map<std::string, Tile> tiles_;   // drawn pieces (panels, paper) by their key, reused while the width is the same
    // optui: rText's pictures (shadow, outline, letters) by text / size / colour / look, placed (dx, dy) pixels from the
    // text's start pixel: a text drawn again anywhere is a copy
    struct TextTile { int dx = 0, dy = 0, w = 0, h = 0; std::vector<uint8_t> rgba; };
    // optui: the static layer's widgets as drawn (their look's key, their rectangle with room for glows / shadows,
    // units; [0] the root) and the layer's key without the tree's number: a settings page built again redraws only
    // where they differ
    struct StaticW { std::string key; int r[4]; };
    std::vector<StaticW> staticWs_;
    std::string staticKey_;
    std::unordered_map<std::string, TextTile> textCache_;
    size_t textCacheBytes_ = 0;
    int tilesW_ = 0;
    uint32_t tilesHue_ = 0, tilesSeed_ = 0;
    // blood2, the GPU menu: the splatter layer (splatterLayer on a worker; splatKey_: what it shows, the job's key)
    std::vector<uint8_t> splatImg_;
    int splatImgW_ = 0, splatImgH_ = 0;
    uint64_t splatGen_ = 0;
    std::string splatKey_, splatJobKey_;
    std::future<std::vector<uint8_t>> splatJob_;
    int splatJobW_ = 0, splatJobH_ = 0;
              // ... and blood colour (bloodHue_: a new colour redraws them)
    int tileDepth_ = 0;                  // drawing into a tile (rCached)
    // draw() into a tile kept under key (its box: x, y, w, h units and a margin), then the tile over the target
    void rCached(const std::string& key, float x, float y, float w, float h, float margin, const std::function<void()>& draw);

    // widgets (0x427530 widget_new, 0x427694 widget_add_child, 0x427C84 widget_free_tree, 0x427CB8 widget_set_rect,
    // 0x4283A0 widget_set_label, 0x4283CC widget_set_text, 0x4283F8 widget_set_image, 0x428424 widget_set_handlers)
    Widget* widgetNew();
    static void addChild(Widget* parent, Widget* child);
    void freeTree();
    static void setRect(Widget* w, int x, int y, int ww, int hh);
    void setLabel(Widget* w, int id, uint32_t flags);
    void setText(Widget* w, const std::string& s, uint32_t flags);
    void setImage(Widget* w, int id, int idHover, uint32_t flags);
    void setHandlers(Widget* w, std::function<void(Widget&)> act, bool (Frontend::*hit)(Widget&, int, int));

    // the rect stack 0xD5AD20 (12-byte entries: widget, x, y, w, h; 0xD5B1D0 the top)
    struct Rect { const Widget* w; int x, y, ww, hh; };
    std::vector<Rect> stack_;
    void stackReset();                    // the walks' start: the screen rectangle
    void push(const Widget& w);           // 0x4275CC
    void absRect(const Widget& w, int& x, int& y, int& ww, int& hh) const;   // the walks' common prologue

    // the walks
    void hoverWalk(Widget* w);            // 0x427998 (from 0x427938)
    void drawWalk(Widget* w);             // 0x427AE8 (from 0x427A84)
    void activateWalk(Widget* w);         // 0x427810 (from 0x4277B0)
    // Light Gun Edition (snapAim): the nearest activatable widget to the pointer within kSnapRange: a point on it whose
    // hit test passes (false: none)
    bool snapPoint(int& x, int& y);
    void snapCollect(Widget* w, int& best, int& bx, int& by);
    bool walking_ = false;                // 0x5CED8C
    bool blink_ = true;                   // 0x5CED90 (frame counter 0x5CEC08 bit 3)

    // drawing
    void drawImage(Widget& w);            // 0x427CDC widget_draw_image
    void drawText(Widget& w);             // 0x427FE8 widget_draw_text
    void drawTextLit(Widget& w);          // ours: drawText in the hover's yellow (the current tab)
    bool hitText(Widget& w, int x, int y);   // 0x428178
    bool hitRect(Widget& w, int x, int y);   // 0x428294
    bool hitImage(Widget& w, int x, int y);  // 0x427E70: on the picture (placed as drawImage)
    void addBossArrows(Widget* root, int mode);   // the boss tables' arrows (0x424D64 / 0x424E10)
    void pcModePanel(Widget* panel, int p);  // 0x420080 (frontend_pcmode.cpp)
    void pcModePanel4(Widget* cell, int p);  // ours: a quarter panel (4P)
    void drawHalfImage(Widget& w);           // ours: the sprite at half size (2 x 2 averaged), centred in the widget
    void drawFrame(Widget& w);               // ours: a 2-pixel frame in the colour w.user (0xRRGGBB)
    void pcModeStep(int p, int dir);         // the arrows' step (0x425B5C / 0x425BAC)
    void pcModeRankColumns(Widget* root, const std::vector<RankRow>& rows);   // screen 9's four columns (frontend_pcmode.cpp)
    void measure(const std::string& s, uint32_t flags, int& w, int& h) const;   // 0x427F64 (of the translation)
    // lang: measure of a text already translated; the language's font on the Classic screens (ttfDraw: capitals' top
    // at y, th their height in the PC font), its size and colour for the PC font of flags; Latin-1 bytes for the PC
    // fonts; an overflow noted (--lang-check)
    void measureRaw(const std::string& s, uint32_t flags, int& w, int& h) const;
    void ttfDraw(const std::string& s, uint32_t flags, int x, int y, int th, bool dim, const render::TtfText* tf,
                 float shrink = 1.0f);   // (leftovers: shrink < 1: smaller, its baseline kept: Widget::fitW)
    float classicEm(uint32_t flags, int th, const render::TtfText* tf) const;
    uint32_t fontColour(uint32_t flags) const;
    mutable std::map<uint32_t, uint32_t> fontColour_;
    static std::string pcBytes(const std::string& s);
    void langOverflow(const std::string& s, int x, int tw, int ww, const Widget* w);
    void drawSmallRaw(const std::string& s, int y, bool dim);   // drawSmall of a text already translated
    void drawW(Widget& w);   // lang: w's draw (a raw widget's text not translated again)
    // lang: the text drawing translates (a language on, or the export recording) unless the caller translated it
    // already (rawText_: a text it cut into lines; RawText while it draws them)
    bool rawText_ = false;
    bool langOn() const { return !rawText_ && lang::on(); }
    struct RawText {
        Frontend* f;
        bool was;
        explicit RawText(Frontend* fe) : f(fe), was(fe->rawText_) { f->rawText_ = true; }
        ~RawText() { f->rawText_ = was; }
    };
    std::string T(const std::string& s) const { return langOn() ? lang::tr(s) : s; }
    // a text translated and cut into lines of at most `width` characters (a CJK one: two); English: wrap(s, width)
    std::vector<std::string> wrapTr(const std::string& s, size_t width,
                                    std::vector<std::string> (*wrap)(const std::string&, size_t)) const
    {
        if (!langOn()) return wrap(s, width);
        const std::string t = lang::tr(s);
        return lang::active() ? lang::wrapColumns(t, int(width)) : lang::noteLines(wrap(t, width));
    }
    static int glyph(uint8_t c, uint32_t flags);      // 0x428494
    static const char* label(int id);                 // 0x428448 (English)
    void drawCursor();                    // 0x420904 / 0x420A10
    void drawHand(int x, int y);          // galfix: the hand at kHandScale, smooth, with its shadow (canvas styles)
    void drawMirrored(int glyph, int x, int y);   // ours: the small font's ')' (its '(' mirrored)
    void drawHole(Widget& w);             // ours: the widget's rectangle transparent (the Gallery's 3D window)
    // gallery3, the Remake layout (gallery_remake.cpp)
    void screenGalleryRemake(const GalleryText& g, Widget* root);
    void drawFill(Widget& w);   // tag: the colour 0xAARRGGBB; id 1: torn top / bottom edges, 2: a key cap (a border)
    void drawTile(Widget& w);   // tag: the entry; id bit 0 the shown one (orange frame)
    void drawCard(Widget& w);   // galleryCard's picture fitted in the rectangle
    void drawDots(Widget& w);   // tag: the pages, id: the shown one
    bool galleryRemake_ = false;
    // regallery, the Remake style's Gallery and Cut-scenes pages (gallery_re.cpp)
    void remakeGallery(const GalleryText& g);
    void remakeScenes(const ScenesText& s, const GalleryText& g);
    void reColumn(const GalleryText& g, int selected);   // the title and the categories down the left (Cut-scenes last)
    struct RePrompt { std::string key, label; std::function<void()> act; int tag = 0; };
    void reLegend(const std::vector<RePrompt>& items, int left, int right);   // the key caps along the bottom
    bool galleryRe_ = false;
    int reFrame_[4] = {}, rePanels_[3] = {};
    // a part that cross-fades / slides when its key changes (the file panel: the entry; the list: the category) and is
    // redrawn in place when only its content (sig) does; draw: its content in units (reAnimate draws it into the part)
    struct ReAnim {
        std::string key, sig, wantKey, wantSig;
        int box[4] = {};   // x, y, w, h units
        std::function<void()> draw;
        RemakeLayers::Part cur, old;
        bool hasOld = false;
        double t0 = -1e9;
        int slide = 1;     // the new one comes from the right (1) / below (2)
    };
    ReAnim reFile_, reList_;
    int reListScroll_ = -1, reListSeen_ = -1, reListN_ = 0;   // menus3: the list's own scroll (-1: round the shown one)
    int reSceneFirst_ = 0, reSceneSel_ = -2, reSceneTop_ = 0;   // the Cut-scenes list: its first shown line (scroll), the scene last chosen, the last scroll
    std::vector<RemakeLayers::Part> reParts_;
    uint64_t reGen_ = 0;
    void reAnimate();
    void drawReRoot(Widget& w);     // the Cut-scenes page's ground (the Gallery's: the host's stage)
    void drawReCat(Widget& w);      // a category (user bit 0: the current one)
    void drawReRow(Widget& w);      // a list row's hover mark (its text is in the list part)
    void drawRePrompt(Widget& w);   // a key cap and its label (value: "key|pad key")
    void drawReOpt(Widget& w);      // a labelled option row: label left, value right (Cut-scenes; the file's toggles)
    float reOptLeftArrow(Widget& w);   // galfix2: a Viewer row's "<" (its middle x, units)
    void drawReButton(Widget& w);   // a slim button: an outlined box, the label centred (user 1: the main one, red)
    void drawReRule(Widget& w);     // a thin rule in the blood colour fading to the right
    // ours: the Gallery's category column (screens 17 and 19): the categories, the shown one in yellow (`selected`;
    // the Cut-scenes page is the last); clicks go to onGallery(100 + c) / the Cut-scenes page
    void galleryColumn(Widget* root, const std::vector<std::string>& categories, int selected);
    void mirrorPage(Widget* root);   // sweep2 (rtl): the page's widgets mirrored left / right, their texts' alignment too
    static constexpr uint32_t kTextYellow = 0x4000000;   // ours: a white-24 text drawn yellow (the chosen item) always
    void drawJukeboxBar(Widget& w);       // ours: the Jukebox's progress bar (w.user: 0-1000)
    std::string jukeboxSig_;              // the Jukebox page's texts when it was built (refreshJukebox)
    void drawSlider(Widget& w);           // 0x426E80
    bool hitSlider(Widget& w, int x, int y);   // 0x426D5C
    void clickSlider(Widget& w);          // 0x426DC0
    std::vector<std::string> texts_;      // the value texts' buffers (0xD5A9A0 / 0xD5A99C)

    assets::PcSprites spr_;
    assets::PcSprites splashSpr_;   // the splash's pictures through TITLE.TBL / TITLE.PAL (Frontend::load)
    std::vector<uint8_t>* canvas_ = nullptr;
    std::vector<std::unique_ptr<Widget>> pool_;
    Widget* root_ = nullptr;              // 0x5CED88
    int screen_ = 0;                      // 0x5CE8A8
    int px_ = 5, py_ = 5;                 // 0xD5A97C / 0xD5A980 (frontend_run starts them at 5, 5)
    int cursorFrame_ = 0;                 // 0x5CECA8
    int clickState_ = 0;                  // 0x5CE8E8: 0 idle, 3 pressed, 4 animating, 5 done (activate)
    int clickTimer_ = -1;                 // 0x5CECAC (-1: no animation)
    uint32_t clickTime_ = 0;              // 0x5CECB0
    bool clickSound_ = false;             // 0x5CECDC
    bool keyHeld_[4] = {};                // 0x5CECB4.. (up, down, left, right held last frame)
    int keyStep_[4] = {};                 // 0x5CECC4.. (16.16 step, x1.5 a frame up to 11)
    int activateMode_ = 0;                // 0x5CED94
    uint32_t frameCount_ = 0;             // 0x5CEC08
    void pointerFrame(const FrontendInput& in, uint32_t nowMs);   // 0x420A60
    uint32_t idleSince_ = 0;              // 0xD5A960: the last activity's timeGetTime (pointer moved, activation)
    bool idleReset_ = true;               // a screen builder set 0xD5A960 (applied at the next update's time)
    bool attractDue_ = false;             // 0x420F2B: frontend_state = 6
    bool splashLogoOnly_ = false;         // liveattract: the logo only (splashStart)
    int splash_ = -1;                     // 0xD5A67C: 0 the SEGA logo, 1 the title art; -1 off (ours)
    uint32_t splashTime_ = 0;             // 0xD5A688: timeGetTime at the stage's start
    int splashSprite_ = 0xA1;             // 0x5CE6B8: the picture 0x419F18 paints
    // feat29: the Remake style's splash: the picture as a 640 x 480 RGBA image (per sprite), scaled to the wide screen
    std::map<int, std::vector<uint8_t>> splashImage_;
    void splashRemake(std::vector<uint8_t>& canvas);
    void splashTick(const FrontendInput& in, uint32_t nowMs);   // message 0xD (0x41A140)
    void clickFrames(uint32_t nowMs);             // 0x42095C
    void escapeKey();                             // frontend_input 0x41DA18: Escape -> the widget tagged 1
    std::string extrasPageTitle_;
    std::string help_;                    // the help line of the widget under the pointer (set by the hover walk)
    bool galleryMirror_ = false;       // sweep2: the Classic Gallery page mirrored (a right-to-left language)
    mutable int galleryRectM_[4] = {};
    bool galleryFull_ = false, galleryInfo_ = true;   // gallery3: the Gallery page's last build (full view, info)
    void drawHelp();                      // ours: help_ centred at the bottom in the 6 x 10 font (two lines at most)
    // ours (uxpolish): the controls line under the help line on every menu screen (dimmed 6 x 10 font): how to point,
    // choose and go back with the mouse, the keyboard and a pad (hintFor: the screen's words; empty: none)
    std::string hintFor() const;
    void drawSmall(const std::string& s, int y, bool dim);   // one centred line in the 6 x 10 font
    // the small EXPERIMENTAL tag at (x, y) of parent (6 x 10 font); its width (0: not placed when it would pass maxRight)
    int experimentalTag(Widget* parent, int x, int y, int maxRight);
    void tabStrip(Widget* root, int y, int current);   // the categories' tabs across the screen at y
    void helpRow(Widget* w, const std::string& h) { w->help = h; }
    int spacing_ = 32;                    // 0x5CE8D8 (THOTD.EXE data: 32)
    bool debugItems_ = false;             // 0x5CD374 (the debug menu's Cheat / Edit Char Stats / Creature Test)
    int mainGroup_ = 0;                   // ours (menu2): the main menu shown: 0 the top, 1 More Modes, 2 Extras
    bool creatureItem_ = false;           // 0x5CE898 (Creature Test)
    char codeBuf_[9] = "00000000";        // 0x5CE89C (reset from 0x5A1198 while Ctrl is up)
    int cheatStage_ = 0, cheatScene_ = 0; // 0xD5B350 / 0xD5B354
    int creatureBo_ = 0, creatureYaw_ = 0;   // 0xD5A944, 0x5CEBF8
    void secretCodes(const FrontendInput& in);   // frontend_input 0x41DA18's tail
    std::string name_;                    // 0x5CE8F0 (10 characters at most)
    int nameCursor_ = 0;                  // 0x5CE910 (the length: 0x5CE914 = name_.size())
    void nameKey(int k);                  // 0x425188's key cases
    void nameDone();                      // Enter / Escape (0x42521E)
    void drawNameField(Widget& w);        // 0x424F7C

    // ---- remakepages (frontend_remake_pages.cpp): Chapter Select, Boss Mode (+ its times tables), PC Mode, High
    // Scores and the Jukebox in the Remake style (the same items and actions as their Classic pages) ----
    bool rPlain_ = false;              // the backdrop untinted (as the main menu's), not the settings pages' tint
    bool rGrid_ = false;               // the arrows move the focus to the nearest item that way (cards, PC Mode)
    std::function<void(int)> rTabFn_;  // the page's own tabs (Q / E, Left / Right): remakeTab calls it
    int rPage_[5] = {};                // the page's torn panel (x, y, w, h; 0 wide: none) and its seed
    int rArt_ = -1;                    // a PC picture centred behind the page (PC Mode: the tombstones 0xA4)
    Widget* rHome_ = nullptr;          // the page's first focus (null: the first nav widget)
    void remakePage(int screen, const std::string& key, bool plain);   // remakeBegin + the page's root
    void remakeFocusDir(int dx, int dy);   // rGrid_: the nearest nav widget that way
    void remakeChapters();
    void remakeBossMode();
    void remakeBossTimes(int mode, int highlight);
    void remakeRanking();
    void remakePcMode();
    void remakePcPanel(int p, int x, int y);    // the PC's two panels: one on a tombstone (x, y its top left)
    void remakePcPanel4(int p, int x, int y);   // 4P: a quarter panel
    void remakeJukebox();
    Widget* rWidget(int x, int y, int w, int h, void (Frontend::*draw)(Widget&), const std::string& text = std::string());
    Widget* rStripButton(int x, int y, int w, int h, const std::string& text, const std::string& help,
                         std::function<void()> act, int seed);
    void drawRPage(Widget& w);         // the root: the splatter, rArt_, the panel rPage_
    void drawRCard(Widget& w);         // a card: torn panel, picture (id; idHover lit), strip (text), line (value)
    void drawRSprite(Widget& w);       // a PC sprite (id) fitted in the rectangle (user 1: half size)
    void drawRStat(Widget& w);         // label left, value right (text, value; user: the text's size)
    void drawRSide(Widget& w);         // a left / right arrow (user 0 left, 1 right; seed 1: dim)
    void drawRScore(Widget& w);        // a table row: text = name, value = "stage|score|character", id: the picture
    void drawRFrame(Widget& w);        // 4P: a seat's frame in its colour (user), a dark fill
    void drawRJRow(Widget& w);         // the Jukebox's list row (user bit 0 chosen, bit 1 playing; value: the number)
    void drawRJBar(Widget& w);         // the Jukebox's progress bar (user 0-1000)
    void drawRBox(Widget& w);          // a small dark torn box (the Jukebox's "now playing")
    void drawRText(Widget& w);         // plain Remake text (user colour, seed: align 0 left / 1 centre / 2 right, h size)
    void drawBackdropShade(Widget& w); // the Gallery's page over the backdrop: a dark veil over the 640 units
};

// --frontend-shot (menuart): the main menu drawn from the PC files with the pointer at (cx, cy), written as a
// width x height PNG (the 640 x 480 picture scaled to the height, pillarboxed). 0 = written.
// screen: 1 the main menu, 2 Options, 3 a settings page (sample values), 4 a Controller page, 8 High Scores (table
// HOTD_FE_PAGE), 9 Chapter Select, 10-15 the categories Video, Audio, Controls, Gameplay, Mods, System (menu_pages.h,
// the default settings; HOTD_FE_PAGE: the page), 16 Co-op, 18 Keys & Buttons (keybind), 20 the Jukebox (sample texts), 21 Quit's question
// (uxpolish), 115 the System category (15 is the name entry), 120 a page setup(fe) builds
// (the Online Co-op pages; false: nothing to shoot)
int frontendShot(const std::string& pcInstall, const std::string& png, int cx, int cy, int width, int height, int screen = 1,
                 const std::function<bool(Frontend&)>& setup = nullptr);
// lang (--export-lang, --lang-check, the language renders): every menu screen in both styles drawn as --frontend-shot
// draws it (1280 x 720; pngDir non-empty: written there as <style>_<screen>.png), then the extra pages (the host's:
// the Gallery's entries, the Online Co-op pages); each(fe, name) after each one; check: the texts wider than their
// boxes noted (Frontend::overflows). The screens drawn
struct LangPage {
    int screen = 0;                            // frontendShot's (17 the Gallery, 120 an Online Co-op page)
    std::string name;
    std::function<bool(Frontend&)> setup;      // its texts (the Gallery's entry, the online scene)
};
int langTour(const std::string& pcInstall, const std::string& pngDir, const std::vector<LangPage>& extra, bool check,
             const std::function<void(Frontend&, const std::string&)>& each);

} // namespace hotd::game
