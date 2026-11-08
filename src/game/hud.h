// The HUD (docs/runtime.md "HUD"). Arcade models: the arcade's own 2D layer, composed each frame from the tile /
// character RAM the original's routines write (src/game/sprites.cpp, src/assets/sprites_arcade.h): torches, bullets,
// RELOAD, CONTINUE / GAME OVER, the chapter / title cards, subtitles, the boss title and life bar, drawn over the 3D
// picture where the arcade's 496 x 384 screen lies in the window. PC models: the PC's own sprites
// (SPRITE\SPR.RBH + BIGGER.VMC pages, assets/sprites_pc.h) at the same places; a labelled text placeholder only when
// those files are missing (the arcade's graphics are never mixed into the PC mode). Both: the players' crosshairs
// (the mouse stand-in for the light gun), the letterbox bars, the chapter clear's results (kind 176's sprites are not
// drawn yet).
#pragma once

#include "game/runtime.h"
#include "game/frontend.h"
#include "assets/sprites_pc.h"
#include "render/overlay.h"

#include <array>
#include <string>
#include <algorithm>
#include <vector>

namespace hotd::game {

// an RGBA picture (the 4P HUD's pieces: Hud::drawSeatHud)
struct SeatImg {
    std::vector<uint8_t> px;
    int w = 0, h = 0;
};

class Hud {
public:
    bool init(std::string& err);
    // display: draw the player's crosshair (the sight)
    bool drawCross[4] = {true, true, true, true};   // players 3 / 4: the experimental 4P mode
    // Light Gun Edition: while paused, player 1's crosshair stays (the pause menu's pointer: the Windows pointer is never
    // shown in the game), whatever Show Crosshairs says
    bool pauseSight = false;
    // each player's crosshair (firing range, game/sights.h SeatSight: style 0 auto (the model source's), 1 arcade
    // style, 2 the PC's own sight sprites, 3.. the drawn ones, 9 custom; size, thickness, colour) and the crosshairs
    // folders (the custom sets)
    SeatSight sights[4];
    // gunfb (Video > Show Crosshairs; LaunchConfig::showCrosshairs): off, no sight drawn for anyone (a player's own
    // Crosshair Off: theirs alone); the sight still counts as shown for the desktop cursor (drewSight: hidden)
    bool showSights = true;
    std::vector<std::string> customDirs;
    // the launcher's aspect is widescreen (full-window flashes) rather than the original 4:3 (pillarboxed)
    bool widescreen = true;
    // bugs1 (#35, Options > Video "Hit Marks"): the hit marks (the blood / claw marks over the screen when a player is
    // hit) true see-through, as the 1998 PC port's Direct3D mode, instead of the arcade's stipple (every other pixel)
    bool translucentMarks = false;
    // spanish3 (Options > Gameplay > Boss Names: Codenames Only, as the Spanish PC release): the bosses' title cards
    // show only the codename ("TYPE 27", moved to where the name began), not "Chariot"; arcade and PC models, every
    // language (the Spanish CD's own cards, "Guerrero" ..., stay as they are)
    bool bossCodenames = false;
    // display2: widescreen: the per-player HUD (Runtime::cellSide) at the window's left / right edges, the rest of the
    // 2D picture centred over the 4:3 part (original 4:3: everything where the arcade's screen lies)
    bool anchorEdges = false;
    // aspect1610: the game area's widest shape (width / height; 0 = the whole window): Aspect 16:10 = 1.6, a window
    // wider than that is pillarboxed (main.cpp pillarbox), so the edge-anchored HUD and the full-picture flashes stop there
    float maxAspect = 0.0f;
    // wide2d (Video > Widen 2D Screens: Stretch): in a wide window the 2D screens made for the 4:3 screen alone (the
    // chapter cards, the game over's route map and GAME OVER card, the chapter clear's wall) are widened from their own
    // pixels up to 16:9 (wideTarget) instead of side bars. False (Original 4:3): as before, byte for byte
    bool widen2D = true;
    // the game area's width in a window ww x wh (the window's, or maxAspect's shape centred in it)
    int areaW(int ww, int wh) const
    {
        return maxAspect > 0.0f ? std::min(ww, int(double(wh) * double(maxAspect) + 0.5)) : ww;
    }
    // set by draw: the player's sight was drawn this frame (the desktop cursor is hidden then: it showed as a grey
    // plus inside the sight, user 2026-10-16)
    bool drewSight[4] = {};
    bool cpu[4] = {};   // a computer teammate's seat: "CPU" under its crosshair
    // the in-game pause (game/pause.h): -1 running, else the selection (0 Continue, 1 Exit). While paused the PC draws
    // no 2D sprites, subtitle or sights (0x418CC8's `if (!0x5CD358)` around pc_sprites_render / 0x41B114 / the sights),
    // only the 3D picture and the pause menu (0x41AB7C, drawPause)
    int pauseSelection = -1;
    // GitHub #5 (Chapter Retry): the pause's own lines (title, then the items) when they are not the PC's PAUSED /
    // Continue / Exit. fxux: with the PC's sprites they are drawn in the PC's menu font (SPR.RBH's white / yellow 24:
    // the pause font has no S / g / R / y), else as text
    std::vector<std::string> pauseLines;
    // fxux: the pause item under a point of the PC's 640 x 480 screen fitted to the window's height (x from the 4:3
    // part's left, y from the top), as the last drawPause laid them out; -1 none
    // Light Gun Edition: easier to shoot: each item owns the whole band up to halfway to its neighbours (the first and
    // the last a further one and a half rows beyond), 100 .. 540 across
    int pauseItemAt(float x, float y) const
    {
        const size_t n = pauseRects_.size();
        if (n == 0 || x < 100.0f || x >= 540.0f)
            return -1;
        for (size_t i = 0; i < n; ++i) {
            const float c = (pauseRects_[i][1] + pauseRects_[i][3]) * 0.5f, hgt = pauseRects_[i][3] - pauseRects_[i][1];
            const float top = i == 0 ? c - hgt * 2.0f : (c + (pauseRects_[i - 1][1] + pauseRects_[i - 1][3]) * 0.5f) * 0.5f;
            const float bot = i + 1 == n ? c + hgt * 2.0f : (c + (pauseRects_[i + 1][1] + pauseRects_[i + 1][3]) * 0.5f) * 0.5f;
            if (y >= top && y < bot)
                return int(i);
        }
        return -1;
    }
    // the cut-scene viewer's Clean View (game/scene_view.h, ours): the 3D picture, the cut-scene bars and the fades only:
    // no 2D layer over the 3D (lamps, ammo, prompts, captions), no players 3 / 4 HUD, no sights
    bool clean = false;
    // trailer (ours): no subtitles / captions (the cut-scene viewer's Subtitles Off, Trailer Mode). Draw-only
    bool noText = false;
    // qa2: the place (0-based) the Survival game's points take in the Survival table at its end (-1: none / not known)
    int survivalRank = -1;
    // rel130: The Typing of the Dead 0's start notice ("EXPERIMENTAL / PROOF OF CONCEPT", the host's 6 s), as
    // Survival's was until survfix3: two lines in the score line's font under the title block
    bool totdNote = false;
    // the sight's window rectangle (x0, y0 top-left, w, h in pixels) for a w x h sight centred on window pixel (x, y)
    // at the PC's size (its 480 lines scaled to the window's height); the input test checks its centre is the aim
    static void sightRect(int w, int h, int wh, float x, float y, float out[4]);
    // aim: the players' crosshairs in window pixels; debug: the developer lines at the top-left
    void draw(const Runtime& rt, int windowWidth, int windowHeight, const float aim[4][2], bool showDebug,
              const std::vector<std::string>& debug);
    // the developer lines' font alone (the cut-scene viewer's fast-forward: a black window and its line)
    void drawLines(int windowWidth, int windowHeight, const std::vector<std::string>& lines)
    {
        debug_.setText(lines, windowHeight >= 720 ? 2 : 1);
        debug_.draw(windowWidth, windowHeight);
    }
    // crt (render/crt.h): draw() leaves the sights out; drawSights draws them (over the filtered picture: crisp)
    bool deferSights = false;
    void drawSights(const Runtime& rt, int windowWidth, int windowHeight, const float aim[4][2]);
    // vr (docs/vr.md): player `seat`'s sight as draw would show it now (the same conditions; drawCross aside), as a
    // texture (built for a wh-pixel-high picture) and its width / height; false: no sight now
    bool vrSight(const Runtime& rt, int seat, int wh, unsigned& tex, float& aspect);
    // before the 3D (arcade models): clears the frame to `clear` and draws the low-priority 2D cells, which the 3D then
    // covers where it draws (MAME model2 screen_update: low cells, polygons, priority cells). Returns false (nothing
    // drawn, the frame left alone) when there are none: the renderer clears as usual.
    bool drawUnder(const Runtime& rt, int windowWidth, int windowHeight, const float clear[3]);
    // menuart: a w x h RGBA picture fitted inside the window, centred (the PC front end's 640 x 480 screen; the menu's
    // overhaul: a window narrower than 4:3 letterboxes it, so nothing of the menu leaves the window)
    void drawPicture(const std::vector<uint8_t>& rgba, int w, int h, int windowWidth, int windowHeight)
    {
        upload(rgba, w, h, windowWidth, windowHeight, 0.0f, true);
    }
    // blood2: a Remake menu screen composed on the GPU (Frontend::gpuLayers): the backdrop (its source a texture, the
    // grey / dark / tint / vignette in a small shader), the widgets' layer (uploaded when redrawn, premultiplied,
    // mipmapped, linear) over the splatter's (a new one cross-fades in, 0.35 s) and the hand, fitted as drawPicture
    void drawRemake(const Frontend::RemakeLayers& L, int windowWidth, int windowHeight);
    // menuperf: the bytes drawRemake has sent to the GPU (texture uploads) so far (the menu's perf tour)
    static inline uint64_t remakeUploadBytes = 0;
    // misc18: the window pixels drawRemake has shaded so far (its full-screen passes and parts; the tour's fill column)
    static inline uint64_t remakeFillPixels = 0;
    static inline bool fullLayers = false;   // misc18 (testing): the layers drawn whole (HOTD_MENU_MASK_CHECK)
    // misc18 (low-end GPUs: fill): which 32 x 32 tiles of the widgets' / splatter's layers have any pixel; the layer's
    // full-screen quad is drawn scissored to the merged tiles (the same quad: the same bytes; the empty parts not shaded)
    struct TileMask {
        int w = 0, h = 0, cols = 0, rows = 0;
        std::vector<uint8_t> on;
    };
    // the rectangle drawPicture puts a w x h picture in: its scale and top-left in window units
    static void pictureRect(int w, int h, float ww, float wh, float& scale, float& x0, float& y0)
    {
        scale = std::min(ww / float(w), wh / float(h));
        x0 = (ww - float(w) * scale) / 2.0f;
        y0 = (wh - float(h) * scale) / 2.0f;
    }
    // the cover over the whole screen now: 0xA1 (black), 0x8D (white) or 0 (galfix4: the live background holds its
    // picture under one, main.cpp liveFrame)
    static uint32_t coverShown(const Runtime& rt);

private:
    Overlay player_[2], centre_, debug_, cross_[4], bar_, boss_, title_, clear_, label_;
    Overlay survival_, survivalBanner_;   // Survival Mode (game/survival.h): the round lines at the top, the moment's banner in the middle
    Overlay survivalSign_[SurvivalMode::kItems];   // Survival's shop signs (between the rounds)
    bool signLit_[SurvivalMode::kItems] = {};
    void drawSurvival(const Runtime& rt, int ww, int wh, int scale);   // (and the weapons' lines)
    void drawTotdNote(const Runtime& rt, int ww, int wh);   // rel130: totdNote's two lines
    // a line in the font of the game's score line (Score Display; the PC's with PC models) on a canvas of the 2D
    // picture's size, at an integer scale, with its shadow: its width
    float scoreText(const Runtime& rt, std::vector<uint8_t>& dst, int W, int H, const std::string& s, float x, float y, int sc,
                    int align, const uint8_t rgb[3], bool measure);
    int barRows_ = -1;
public:
    // Light Gun Edition: the game picture's width in window pixels (4:3 / 16:10 with the bezel: the cut-scene bars stay
    // inside it, not over the bezel); 0: the whole window
    float barWidth = 0;
private:
    // the arcade 2D layer: one 496 x 384 texture, drawn over the 3D picture
    unsigned program_ = 0, vao_ = 0, texture_ = 0;
    int uRect_ = -1;
    int uTint_ = -1;
    int uUV_ = -1;     // wide2d: the quad's texture range (kVs uUV)   // rainbow2: the sights in the rainbow (a > 0.5: the picture's brightest channel times rgb)
    bool sightTintOn_[4] = {};
    float sightTint_[4][3] = {};   // (rainbow2: each player's sight colour this frame, drawSights)
    std::vector<uint8_t> hudMask_;   // rainbow2: the arcade layer's HUD / hit-mark cells alone (rainbow2D)
    std::vector<uint8_t> cellsHud_;  // (rainbow2: Runtime::cellHud and the score lines)
    // rainbow2 (Rainbow Blood): the arcade layer's hit marks (and with its HUD option the player HUD) in the rainbow, in
    // the pass's pictures (the pixels of those cells that show)
    void rainbow2D(const Runtime& rt, const uint8_t* tiles, bool high, std::vector<uint8_t>* const* pics, int n);
    // bugs1 (#35): translucentMarks for the arcade layer's hit marks in the pass's pictures (as rainbow2D finds them)
    void translucent2D(const Runtime& rt, const uint8_t* tiles, bool high, std::vector<uint8_t>* const* pics, int n);
    std::vector<uint8_t> markMask_, markLayer_;   // (translucent2D: the marks' cells alone; drawPc2D: one PC mark)
    // blood2 (drawRemake): the backdrop's program and textures (backdrop, widgets now / before a new splatter, hand)
    unsigned bgProg_ = 0, fgProg_ = 0, bgTex_ = 0, fgTex_ = 0, splatTex_[2] = {}, handTex_ = 0;
    int uFgRect_ = -1, uFgAlpha_ = -1, uFgShadow_ = -1, uBgRect_ = -1, uBgUV_ = -1, uBgGrey_ = -1, uBgMul_ = -1, uBgAdd_ = -1, uBgFade_ = -1, uBgAlpha_ = -1;
    uint64_t bgGen_ = ~0ull, fgGen_ = ~0ull;
    int fgW_ = 0, fgH_ = 0;   // leftovers: fgTex_'s size (a redrawn part of the same size: only that part uploaded)
    const uint8_t* bgSrc_ = nullptr;
    int bgW_ = 0, bgH_ = 0, handId_ = -1, splatCur_ = 0;
    uint64_t splatGen_ = 0, partsGen_ = ~0ull;
    std::vector<unsigned> partTex_;          // the focus / hover / help parts' textures
    std::vector<unsigned> animTex_;          // regallery: the Remake Gallery's fading parts (RemakeLayers::anim)
    uint64_t animGen_ = ~0ull;
    bool splatOld_ = false;                  // the splatter before (splatTex_[splatCur_ ^ 1]) fading out
    TileMask fgMask_, splatMask_[2];         // misc18: the widgets' / splatters' tiles with pixels
    double splatFrom_ = -1;                  // the cross-fade's start (ms, steady clock)
    std::vector<uint8_t> premul_;
    std::vector<uint8_t> rgba_, sideRgba_[2];   // sideRgba_: the PC's left / right corner cells (display2)
    std::vector<uint8_t> arcadeSides_[3];       // perf: the arcade pass by side (centred, left, right; Arcade2D::composeSides)
    void drawArcade2D(const Runtime& rt, int ww, int wh);
    void drawPlaceholder(const Runtime& rt, int ww, int wh, int scale);
    // the PC models: the PC's own sprites (SPR.RBH + BIGGER.VMC pages) at the places the arcade's routines put the
    // matching arcade sprites (Runtime::pcCells), the subtitles in the PC's font
    void drawPc2D(const Runtime& rt, int ww, int wh);
    // the 4P mode (docs/coop4.md): players 3 / 4's rounds and lives (fire lamps) at the top-left / top-right corners,
    // player 1's / 2's own HUD art (arcade cells or the PC's sprites) recoloured green / yellow (player_colours.h)
    void drawSeatHud(const Runtime& rt, int ww, int wh);
    // Light Gun Edition: Arsenal Mode: a weapon's rounds (up to 3 bullets in its colour: shotgun red, machine gun
    // silver, rocket green) and their count in the players' label digits, where the bullets are
    void drawArsenalAmmo(const Runtime& rt, int ww, int wh);
    // Light Gun Edition: Coin-Op Survival's clock: the seconds left in the game's own countdown digits (the CONTINUE?
    // ones), top centre of the picture; red and blinking under 10 s
    void drawClock(const Runtime& rt, int ww, int wh);
    // Light Gun Edition: the mods on in this game (Runtime::lgBanner) for 5 s once play starts: their names in the PC's
    // menu font, centred, one a line, white and red in turn
    void drawModBanner(const Runtime& rt, int ww, int wh);
    int seatDy_[2] = {-0x25, -0x25};   // players 3 / 4's band offset as drawSeatHud placed it last
    SeatImg arsenalBullet_[9][2];      // [colour: Runtime::lgEngineTint][arcade, PC]
    SeatImg arsenalDigit_[4][10][2];   // [seat][digit][arcade, PC]
    // GitHub #25: players 1 / 2 with more than 5 lives: "x N" beside their one torch (the name entry's X 0x1AC and the
    // results' digits 0x181.., drawn as drawSeatHud draws its pieces: no game banks used)
    void drawLivesCount(const Runtime& rt, int ww, int wh);
    std::vector<uint8_t> seatTiles_, seatRgba_;
    SeatImg seatLabel_[2][2];   // "3P" / "4P" [seat - 2][arcade, PC], built once from the art
    uint64_t seatEmptySince_[2] = {};   // players 3 / 4's RELOAD! prompt: the game frame (+ 1) their gun went empty, 0 none
    Overlay seatText_[2];
    Overlay cpuTag_[4];   // "CPU" under a computer teammate's crosshair, in its colour   // players 3 / 4's CONTINUE? when its sprite bank is not loaded (no one continued yet)
    // dx: window pixels to the right (the anchored corners)
    void upload(const std::vector<uint8_t>& rgba, int w, int h, int ww, int wh, float dx = 0.0f, bool fit = false,
                int wide = 0, int wideW = 0);
    // wide2d: the 4:3-only 2D screen shown now (kWide*) and the width it fills (wideW: the game area's, at most 16:9);
    // kWideNone when Widen 2D Screens is off, in 4:3, or the window is not wider than the 4:3 screen
    enum { kWideNone, kWideCard, kWidePicture, kWideWall };
    int wideScreen(const Runtime& rt, int ww, int wh, int& wideW) const;
    // wide2d: the arcade layers in `layers` (bit n: layer n) composed alone into out
    bool composeLayers(const Runtime& rt, const uint8_t* tiles, bool high, unsigned layers, std::vector<uint8_t>& out);
    // wide2d (the chapter clear's wall): a tiling W x H picture extended to wideW with its own tile (false: no period)
    bool tileExtend(const std::vector<uint8_t>& pic, int W, int H, int ww, int wh, int wideW, bool sidesOnly);
    std::vector<uint8_t> layerTiles_, wideBg_, wideTop_, wideExt_;
    void pcFadeDraw(float level, int ww, int wh);
    void fillRect(const uint8_t rgb[3], int ww, int wh, int y0, int y1, int x0 = 0, int x1 = -1);   // window rows y0..y1 (top 0), columns x0..x1 (-1: all), opaque
    // widescreen: the window outside the 4:3 screen black (a 4:3 card; grey 1: white); sides: bit 0 the left, bit 1 the right
    void blackSides(int ww, int wh, float grey = 0.0f, int sides = 3, int gw = 0);   // (gw: the kept width; 0 the 4:3 screen's)
    // credits43: the ending's full-height 2D card (the staff roll's, GAME OVER) is up; rgba_: the 2D just drawn (W x H).
    // The sides to black (blackSides' bits): 0 none; the staff roll only the right (bugs1 #34)
    int endingCard(const Runtime& rt, int W, int H) const;
    void drawPause(int selection, int ww, int wh);
    std::vector<std::array<float, 4>> pauseRects_;   // (fxux: the items' rectangles, 640 x 480 units: x0, y0, x1, y1)
    // lang (docs/translating.md): the cut-scene subtitles with a translation (sub.<event>.<line>): the game's own line
    // hidden (its text cells / the PC's line), the translation drawn in the language's font over the 4:3 screen
    std::vector<std::string> subLines(const Runtime& rt);   // the translations shown now (empty: none)
    void drawSubtitles(const std::vector<std::string>& lines, int ww, int wh);
    std::map<std::string, std::vector<int>> subIndex_;      // an English line -> its keys (event * 2 + line - 1)
    bool subIndexed_ = false;
    bool subHide_ = false;                                   // this frame: the game's own line hidden
    std::vector<uint8_t> subRgba_;
    std::vector<std::string> subShown_;
    void drawBossClock(const Runtime& rt, int ww, int wh);   // Boss Mode's clock (THOTD.EXE 0x418CC8)   // THOTD.EXE 0x41AB7C
    Overlay langPc_[2];                               // lang: the PC font's words (Continues, Press SPACE) in the language
    Overlay pause_;                                   // drawPause without the PC sprites: the same lines as text   // PC models: the PC's screen fade (Runtime::PcFade)
    // display2: the arcade layer's pass (high / low priority) centred, and in widescreen its side cells
    // (Runtime::cellSide) moved to the window's edges; false when nothing was drawn
    // composed (perf): rgba_ already holds this pass's whole picture (drawUnder's check), used as it is when centred
    std::vector<uint8_t> tilesCopy_;
    const uint8_t* filteredTiles(const Runtime& rt);   // tile RAM without the cells clean / noText hide
    // the 3-4 player mode: tiles without player 2's hit notice (drawSeatHud draws it again in player 2's colour; the
    // arcade's splash is see-through, a blue copy over the red one came out purple); `tiles` itself when none shows
    const uint8_t* withoutP2Notice(const Runtime& rt, const uint8_t* tiles);
    std::vector<uint8_t> tilesNoP2_;
    // player 2's hit notice now (kPopups record 1): its sprite and top-left cell; false: none
    static bool p2Notice(const Runtime& rt, uint32_t& sprite, int& x, int& y);
    bool drawArcadePass(const Runtime& rt, int ww, int wh, bool high, bool composed = false);
    // the game area's left edge (the window's, or areaW's) relative to the picture's (W x H fitted to the window's
    // height, centred)
    float leftEdge(int W, int H, int ww, int wh) const { return float(areaW(ww, wh)) / 2.0f - float(W) * float(wh) / float(H) / 2.0f; }
    // spanish3 (docs/spanish.md): Español with a Spanish PC copy and the arcade models: each arcade sprite whose PC
    // picture (kArcadeToPcSprite) lies on a translated page of the Spanish BIGGER.VMC is drawn as that PC picture,
    // scaled to the arcade's cells (PC 10 pixels a cell -> 8). spanishCut: the tiles without those sprites' cells
    // (esCuts_: the sprites, rebuilt each call; `tiles` itself when none); composeEs: Arcade2D::compose of `layers`
    // (bit n: layer n) with the PC pictures between the layers (a layer's pictures over it, under the layers above)
    struct EsCut { int layer, x, y, id; uint32_t clip; uint8_t side; bool high; };
    std::vector<EsCut> esCuts_;
    std::vector<uint8_t> tilesEs_, esLayer_, esPc_;
    // the arcade sprites drawn so (their PC picture differs from the US one: words, not only the re-saved colours),
    // found when the PC sprites are loaded with the Spanish pages (esLoaded_: that BIGGER.VMC; "" none)
    std::vector<uint8_t> esSprite_;
    std::map<int, std::vector<uint8_t>> esImg_;   // PC sprite -> its Spanish picture (PcSprites::image, the key cleared)
    std::string esLoaded_ = "-", pcEs_ = "-", esTraced_;   // (pcEs_: the Spanish file pcSprites_ were loaded with)
    void findEsSprites();
    const uint8_t* spanishCut(const Runtime& rt, const uint8_t* tiles);
    // spanish3 (bossCodenames): the boss name cards' first codename cell (0: not a name card) and the tiles with their
    // codename moved over the name (tilesBoss_; `tiles` itself when none shows)
    static int bossCodenameCell(uint32_t sprite);
    const uint8_t* codenameTiles(const Runtime& rt, const uint8_t* tiles);
    std::vector<uint8_t> tilesBoss_;
    bool composeEs(const Runtime& rt, const uint8_t* tiles, bool high, unsigned layers, std::vector<uint8_t>& out,
                   const uint8_t* side = nullptr, int want = 0);
    // the sights (firing range: one texture per player, built from its SeatSight by game/sight_art.cpp, again when the
    // settings, the models or the window height change): the arcade style (ours: the arcade has none, it is a light-gun
    // game), the PC's sprites 0x45E / 0x45F, the drawn ones (crisp window pixels), a custom set; its pixel (cx, cy) on
    // the aim. seatOk_: a picture was made (else the text cross)
    unsigned seatTex_[4] = {};
    std::string seatKey_[4];
    bool seatOk_[4] = {}, seatSprite_[4] = {};
    int seatW_[4] = {}, seatH_[4] = {};
    int seatDw_[4] = {}, seatDh_[4] = {}, seatCx_[4] = {}, seatCy_[4] = {};
    bool buildSeatSight(int seat, bool pcModels, int wh);
    void drawSight(int player, int ww, int wh, float x, float y);
    std::string pcDir_;
    int blood_ = 0;
    assets::PcSprites pcSprites_;
    bool pcTried_ = false;
    // pcsparks: the frame whose PC 2D layer drawUnder drew under the 3D (the game-over route map), else -1
    long long pcUnderFrame_ = -1;
    void loadPcSprites();

public:
    // the PC install for the PC sprites (model source PC; empty: none); loads them now (the GL context must be current)
    // blood: PcConfig::blood (display2: the sprites from that blood set)
    void setPcDir(const std::string& dir, int blood = 0);
};

} // namespace hotd::game
