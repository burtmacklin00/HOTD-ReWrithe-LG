// ReWrithe's Gallery (ROADMAP "Future: gallery mode"; our own, not in either original): the game's characters, creatures,
// bosses, items and cut content one at a time in 3D, from the player's own files (arcade ROMs or the 1998 PC release,
// switchable), turning, zoom, their motions (the arcade's skeletons and motions, assets/arcade_anim.h, pose both
// sources: the PC creature meshes share the arcade's object IDs), an info panel (name, chapters, hit points, a short
// line of our own) and a choice of backgrounds (the game's own skies, plain colours). The front end draws the page
// around it (Frontend::screenGallery: the categories on the left, the Cut-scenes page is the last one); this class
// loads the models (the chapter stage each one appears in, per source), poses them and draws them into a window
// rectangle. (gallery2, 2026-10-21: categories, info panel, items, cut content, backgrounds, smooth turn / zoom.)
// (The PC release hides a simpler viewer of its own, Creature Test: Ctrl + CREATURE at the main menu, THOTD.EXE
// 0x422324; tip from YouTube commenter RUNESA. Its 1:1 port is separate; this is our own mode.)
#pragma once

#include "assets/arcade_anim.h"
#include "assets/arcade_loader.h"
#include "assets/pc_loader.h"

#include "game/cosmods.h"

#include "render/renderer.h"

#include <array>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace hotd::game {

// the Gallery's categories, in the page's order (the Cut-scenes page, Frontend::screenScenes, comes after them)
enum GalleryCategory : int { kGalPlayers, kGalHumans, kGalCreatures, kGalBosses, kGalItems, kGalCut, kGalCategories };
const char* galleryCategoryName(int c);   // "Players", "Humans", ... "Cut Content"

struct GalleryEntry {
    const char* key;        // stable name for launcher.ini (gallery=): "t11", "coin" ...
    const char* name;       // the official name where one is known (docs/names.md), else the developers' (marked)
    int category;           // GalleryCategory
    int type;               // arcade creature type (assets/arcade_categories.cpp); -1: still models (`models`)
    int chapter;            // 0-3: the chapter stage its models / texture sets are loaded from
    const char* motions;    // motion name prefixes ("MO_z_ben_ MO_z_walk"): its own motions first; empty: every fitting one
    const char* where;      // the chapters it appears in ("1, 2"), "" not known
    const char* facts;      // hit points / notes from the decompilation ("" none)
    const char* text;       // the description (ours, plain; guesses say so)
    const char* models = "";   // still models: object IDs in hex, one per step ("D12 D13": the damage steps)
    int area = 0;              // still models: the arcade texture area they are drawn in (0: the model's own set)
    const char* rows = "";     // still models: the colour rows of that area ("row:entry ...", as GalleryLook)
    bool clay = false;         // drawn untextured in one light colour (cut types whose textures the game never loads)
    float yaw = 0, pitch = 0;  // still models: their front (degrees: a turn from the first view; the pitch, 0 the usual)
};
const std::vector<GalleryEntry>& galleryEntries();

// the in-game look of a creature type (galleryLook, gallery.cpp): the arcade texture area it is drawn in (0: the model's own set)
// and the colour rows the event scripts swap in there ("row:entry ...", Runtime::colorRows' keys)
struct GalleryLook {
    int area = 0;
    const char* rows = nullptr;
};
GalleryLook galleryLook(int type);

// gallery blood (user 2026-11-03: "look at a creature in each blood colour"): the Gallery's own blood settings, seeded
// from Options (Blood Color, its custom colour, Blood Particles) and Mods (Rainbow Blood) each time the Gallery opens and
// never saved: what the player tries here does not change the game's settings
struct GalleryBlood {
    int color = 0;            // Options' Blood Color: 0 Red, 1 Green, 2 Blue, 3 Purple, 4 Yellow, 5 Custom
    int rgb = 0xFFFFFF;       // the custom colour 0xRRGGBB
    RainbowBloodSettings rainbow;   // Rainbow Blood (on, Colours, Speed, Creature Blood)
    bool particles = true;    // Blood Particles: the drips from the cut joints and the burst of a new damage step
    bool operator==(const GalleryBlood& o) const
    {
        return color == o.color && rgb == o.rgb && rainbow.on == o.rainbow.on && rainbow.together == o.rainbow.together &&
               rainbow.speed == o.rainbow.speed && rainbow.creature == o.rainbow.creature && particles == o.particles;
    }
};
const char* galleryBloodColorName(int c);      // "Red" ... "Custom"
const char* galleryRainbowSpeedName(float s);  // "Still" / "Slow" / "Normal" / "Fast"
float galleryRainbowSpeedStep(float s, int dir);
const char* galleryCreatureBloodName(int c);   // "Own Colour Each" / "Rainbow Cycle" / "Off"

// gallery dismemberment (user 2026-11-03): the damage and part-loss models the game swaps in when a part is shot, from
// its own tables (part_hit 0x68010: part_replacement 0x11DFF0 by type and part class, the per-type damage routines
// 0xC66A0 that cut the head / arms off at their models, the head's blow-off step 0x135940), grouped by body part
enum GalleryPart : int { kPartHead, kPartRightArm, kPartLeftArm, kPartBody, kPartLegs, kGalleryParts };
const char* galleryPartName(int p);   // "Head", "Right Arm" ...

// the window's backgrounds (Gallery::background): 0 the dark default, then the game's skies, then plain colours
int galleryBackgroundCount();
const char* galleryBackgroundName(int b);

class Gallery {
public:
    struct Paths {
        std::string regions, dumps, texsets;   // the arcade's (GameView::init's), empty regions: no arcade source
        std::string pc;                        // the PC install, empty: no PC source
        int blood = 0;                         // PcConfig::blood
    };
    // the animations and the renderer (a GL context must be current); the stages load on first use
    bool open(const Paths& p, std::string& err);
    void close();   // GPU objects and every loaded stage
    bool isOpen() const { return open_; }

    int count() const { return int(galleryEntries().size()); }
    int index() const { return index_; }
    void select(int i);
    bool selectKey(const std::string& key);    // the entry with that key (false: none, nothing changes)
    std::string key() const { return galleryEntries()[size_t(index_)].key; }
    // previous / next entry inside the current category (wraps)
    void step(int dir);
    // categories: the current one, the entry's place in it, another one (its last-seen entry, else its first)
    int category() const { return galleryEntries()[size_t(index_)].category; }
    int indexInCategory() const;
    int countInCategory(int c) const;
    void selectCategory(int c);
    Source source() const { return source_; }
    bool hasSource(Source s) const { return s == Source::Arcade ? bool(arcade_) : bool(pc_); }
    void setSource(Source s);
    void toggleSource() { setSource(source_ == Source::PC ? Source::Arcade : Source::PC); }
    void nextMotion(int dir);
    // gallery3 (user 2026-10-21, a model viewer like Resident Evil Requiem's): the motion and the slow turn are off
    // until the player turns them on (Space / T); the object lives for the whole run, so the choice is kept for the
    // session (not saved)
    bool playing = false;    // the motion runs (false: holds its frame)
    bool autoTurn = false;   // the model turns slowly (turning it by hand stops it)
    int light = 0;           // the light: 0 the game's (straight up), 1-8 from the side, 45 degrees apart round the view
    void nextLight() { light = (light + 1) % 9; }
    std::string lightLabel() const;   // "Game" / "Side 1" ...
    int background = 0;      // galleryBackgroundName
    bool clayCut = false;    // the cut creatures untextured, as the Gallery first drew them (key U; gallery_cut.h)
    void cycleBackground(int dir);
    // regallery (the Remake style's Gallery, Frontend::galleryRE): the plain backgrounds as a dark stage in the manner
    // of the Resident Evil remakes' galleries: a soft light on the wall behind the model and a pool of it on the floor,
    // a faint floor reflection, the model fading / sliding in on a new entry; rePanels (window pixels): the page's left
    // column's right edge, its file panel's left edge (>= the width: none) and its controls' top, shaded over it all
    bool reStage = false;
    float rePanels[3] = {};

    // dyaw / dpitch in radians; zoom: factor (>1 nearer). The view eases to the wanted angles / distance (smooth);
    // turnNow: the mouse drag, at once
    void turn(float dyaw, float dpitch);
    void turnNow(float dyaw, float dpitch);
    void zoom(float factor);
    // pan: the model moved on the screen (dx right, dy down) in the rectangle's heights, eased; panPixels: by pixels
    // of the last drawn rectangle, at once (the mouse)
    void pan(float dx, float dy);
    void panPixels(float dx, float dy);
    void resetView();   // the front, the first zoom, no pan
    void settle() { yaw_ = wantYaw_, pitch_ = wantPitch_, dist_ = wantDist_, panX_ = wantPanX_, panY_ = wantPanY_, fadeIn_ = 1; }   // the view where it eases to, at once

    void update(float dt);
    // draws the model into the window rectangle (x, y from the window's top left, pixels); ww x wh the window.
    // gallery3: the plain backgrounds softly lit (brighter round the model, a shadow under its feet), a vignette over
    // every one; lift: the bottom part of the rectangle kept for text (a fraction: the model is framed above it)
    // frame (x, y, w, h, window pixels, optional): the model framed there instead, the backdrop still over (x, y, w, h)
    void draw(int ww, int wh, int x, int y, int w, int h, float lift = 0, const int* frame = nullptr);

    // ---- gallery3, the Remake layout (Frontend::screenGalleryRemake; optional, launcher.ini gallerylayout=1) ----
    // the entries' portraits for its grid: kThumbW x kThumbH RGBA (transparent round the model; Cut Content: a dark
    // silhouette), made a few per frame by makeThumbs (call before the frame is drawn: it draws in the window's corner)
    static constexpr int kThumbW = 50, kThumbH = 70;
    const std::vector<uint8_t>* thumb(int entry) const;
    void makeThumbs(int ww, int wh, int category, int most);
    // pmfix2: a portrait of creature type `type` (the PC Mode extras'; game/pm_portraits.cpp) drawn as entry `entry`
    // (its chapter stage; the type's own skeleton, motions `motions` and look), w x h RGBA, alpha 0 round the model; the
    // camera: yaw / pitch in degrees from its front, dist and panY as the window's view (1: the whole model framed)
    bool makePortrait(int type, int entry, const char* motions, int w, int h, float yawDeg, float pitchDeg, float dist,
                      float panX, float panY, std::vector<uint8_t>& rgba);
    // portraits2: makePortrait's head-aimed camera (the PC's portraits: the same head size and place for every body):
    // fovY in degrees; dist / panX / panY then in head heights from the head's middle; the head turned round the neck
    // (degrees, world axes), the motion's frame, the light (degrees round from the camera's side, its height; < 0:
    // straight down as the game's creatures)
    struct PortraitHead {
        bool on = false;
        float fovDeg = 30, headYaw = 0, headPitch = 0, headRoll = 0;
        int frame = 1;
        float lightDeg = -1, lightUp = 0.574f;
        float armDrop = 0, armDropX = 0;   // (the shoulders turned down: round the world z / x axes)
    };
    PortraitHead portraitHead;
    // the entry's weak-point card: the boss intro's own pictures (the arcade's sprites drawn from the PC files' copies;
    // null when the entry has none or there are no PC files)
    const std::vector<uint8_t>* card(int& w, int& h);
    // a 640 x 480 RGBA page drawn in three parts: [0, 200) at the window's left edge, [200, 440) in the middle, [440,
    // 640) at the right edge, at the window's height (4:3 and narrower: as one picture). splitRect: the parts' window
    // x and the scale (Frontend's coordinates -> window pixels)
    void drawSplit(const std::vector<uint8_t>& rgba, int ww, int wh);
    static void splitRect(float ww, float wh, float& scale, float x0[3], float& y0);

    std::string name() const;
    std::string text() const;
    std::string group() const;         // the category's name
    std::string where() const;         // "Chapters 1, 2" / "" not known
    std::string facts() const;
    std::string motionLabel() const;   // "3 / 12:  Walk  (z_walk_g)"; still models: "Step 2 / 4" or ""
    std::string sourceLabel() const;   // "Arcade" / "PC"
    std::string status() const { return status_; }   // empty, or why nothing is drawn

    // ---- gallery blood (GalleryBlood: the Gallery's own, seeded at open by the host) ----
    const GalleryBlood& blood() const { return blood_; }
    // a change shows at once: the arcade models' blood entry and wound rows next frame, the PC models' blood pages
    // reloaded when their colour changes (the PC loader tints them at the load, as the game does)
    void setBlood(const GalleryBlood& b);
    std::string bloodLabel() const;    // "Green", "Custom", "Rainbow"

    // ---- gallery dismemberment (GalleryPart; creatures only) ----
    // per body part a state: 0 whole, 1 .. damageSteps the game's damage models in order, damageSteps + 1 "Off" when
    // partCanGo (the part cut away, as the game's damage routines / the head's blow-off do). A body step the game ties
    // to a part loss (the zombies' last chest model: the head and both arms) takes those parts with it, as in play.
    // Every part whole again on a new entry
    bool hasDamage() const { return dmg_.any; }
    int damageSteps(int part) const;
    bool partCanGo(int part) const;
    int damageStates(int part) const { return damageSteps(part) + (partCanGo(part) ? 1 : 0); }   // (the last state)
    int damage(int part) const { return part >= 0 && part < kGalleryParts ? dmgStep_[part] : 0; }
    void stepDamage(int part, int dir);   // wraps
    void resetDamage();
    std::string damageLabel(int part) const;   // "Whole", "Damage 2", "Off", "-" (no models)
    std::string damageNote() const;            // "" or why there is nothing to change ("No damage models ...")
    int damagePart = 0;   // the part the keys step (Z: the next part, X / Shift+X: its state; Shift+Z: all whole)

private:
    const StageData* stage(Source s, int chapter);
    void pick();   // the entry's motions and framing
    bool open_ = false;
    Paths paths_;
    std::unique_ptr<ArcadeSource> arcade_;
    std::unique_ptr<PcSource> pc_;
    std::map<std::pair<int, int>, std::unique_ptr<StageData>> stages_;   // (source, chapter)
    std::set<std::pair<int, int>> failed_;
    CreatureAnimations anim_;
    Renderer renderer_;
    RenderOptions opt_;
    Source source_ = Source::Arcade;
    int index_ = 0;
    std::array<int, kGalCategories> lastInCategory_{{-1, -1, -1, -1, -1, -1}};
    std::vector<int> motions_;   // creatures: motion numbers; still models: object IDs (the steps)
    int motion_ = 0;      // index into motions_
    float frame_ = 1;
    float yaw_ = 0, pitch_ = 0, dist_ = 1;            // the view now
    float wantYaw_ = 0, wantPitch_ = 0, wantDist_ = 1;   // where it eases to
    float panX_ = 0, panY_ = 0, wantPanX_ = 0, wantPanY_ = 0;   // the pan, in the rectangle's heights
    int lastH_ = 1;       // the last drawn rectangle's height in pixels (panPixels)
    unsigned backProgram_ = 0, backVao_ = 0;   // the backdrop / vignette pass (gallery3)
    int uArea_ = -1, uBase_ = -1, uShadow_ = -1, uMode_ = -1, uStage_ = -1, uPanels_ = -1;
    float stage_[4] = {};    // regallery: the stage's light (window pixels from the bottom: the feet x, y, the model's height; alpha)
    float fadeIn_ = 1;       // regallery: a new entry's model fading in (0 .. 1)
    std::vector<Placement> mirror_;   // regallery: the floor reflection's placements
    bool backdropInit(std::string& err);
    // mode 0: the lit backdrop with the floor shadow (opaque); 1: the vignette (blended over)
    void backdropDraw(int mode, int ww, int wh, int x, int y, int w, int h, const float base[3], const float shadow[4]);
    float center_[3] = {}, radius_ = 10, halfH_ = 10;   // the framing box: centre, half width across, half height
    float tick_ = 0;      // frames since the Gallery opened (the hook models: wings, eyes, veins, the saw)
    std::map<std::pair<int, int>, std::string> xlatRows_;   // (chapter, texture set) -> the colour rows in its translation
    static constexpr uint32_t kSkinKey = 0xF0000000;   // + type: the generated skin model of the arcade stages
    bool framed_ = false;
    std::vector<Xform> scratch_;
    std::vector<Placement> placements_;
    std::vector<Placement> skyPlacements_;
    std::vector<Placement> roomPlacements_;   // gallery3: a room background's world models
    bool thumbMode_ = false;                  // drawing a portrait: no backdrop, the key colour around it
    GalleryEntry portraitEntry_{};             // pmfix2: makePortrait's entry (its type / motions); used while portraitOn_
    bool portraitOn_ = false;
    const GalleryEntry& entryNow() const;
    std::map<std::pair<int, int>, std::vector<uint8_t>> thumbs_;   // (source, entry) -> portrait (empty: none)
    std::map<std::string, std::vector<uint8_t>> cards_;            // entry key -> card (w, h first as 2 x u16)
    unsigned splitTex_ = 0;
    int uPos_ = -1, uUv_ = -1;
    bool roomDebug();   // testing: HOTD_GALLERY_RAIL (a room from a camera rail's frame)
    std::string status_;

    // gallery blood
    GalleryBlood blood_;
    bool bloodSet_ = false;             // setBlood called (else the models as before: Paths::blood, the stages' own colours)
    int pcBlood_ = 0;                   // the PcConfig::blood pc_ was made with
    int pcBloodWanted() const;          // the one blood_ asks for
    std::map<int, std::array<int, 4>> bloodRowEntries_;   // GGSET lists 0xC5A40: a red blood row entry -> per colour
    uint16_t bloodEntry_ = 0;           // polygon palette entry 1023 last sent (the blood colour, arcade models)
    uint32_t bloodRowMask_ = 0;         // polish: the shown look's blood rows (RenderOptions::bloodRowMask, as GameView's)
    std::string lookRows(const char* rows, std::set<int>& bloodRows) const;   // a look's rows in the blood colour
    // gallery dismemberment: the shown type's tables (pick), the parts' states, the effects
    struct DamageData {
        bool any = false;
        uint32_t chain[16][8] = {};   // part_replacement by part class: the model of each damage step (0: none)
        int len[16] = {};
        uint32_t routine = 0;         // part_damage_callbacks 0xC66A0: the type's damage routine (0 none)
        int blowOff = 0;              // part_blow_off_step 0x135940: the head's step that knocks it off (0 none)
        int steps[kGalleryParts] = {};
        bool canGo[kGalleryParts] = {};
    } dmg_;
    int dmgStep_[kGalleryParts] = {};
    bool noDamage_ = false;   // (the framing: the whole body)
    void damageInit();
    // the models drawn for the nodes (0: none) and the cut points (nodes whose part is gone, their parent kept)
    void damageModels(const Skeleton& sk, std::vector<uint32_t>& model, std::vector<bool>& gone) const;
    struct Fx { int kind; float start; float pos[3]; int node; };   // kind 0 a burst (on node), 1 a drip (at pos)
    std::vector<Fx> fx_;
    std::vector<int> cutNodes_;   // the last drawn cut points (the drips)
    float nextDrip_ = 0;
    std::vector<Placement> fxPlacements_;
};

} // namespace hotd::game
