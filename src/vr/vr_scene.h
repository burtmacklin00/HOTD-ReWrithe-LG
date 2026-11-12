// VR mode: the VR frame (docs/vr.md). Per frame: the backend's poses; the 2D HUD into the screen-plane panel; the
// game's 3D items drawn once per eye (GameView::drawOverride: the game builds its list once, the renderer draws it
// from each eye: Renderer::eye); over the world the player's hands (the game's own models on the controllers), the laser,
// the game's own crosshair sprite at the ray's hit, the panel, the comfort vignette; then the backend's hand-over and
// the desktop picture. The gun's aim for the next game tick comes out of it (takeAim).
#pragma once

#include "assets/assets.h"
#include "vr/vr.h"
#include "vr/vr_aim.h"
#include "vr/vr_math.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace hotd {
struct DrawItem;
struct Camera;
} // namespace hotd
namespace hotd::game {
class GameView;
class Runtime;
class Hud;
struct FrameSnapshot;
} // namespace hotd::game

namespace hotd::vr {

// the hands drawn on the controllers: the game's own models from the player's game data (arcade ROMs or the PC
// files, whichever source draws the game), no models of ours. 0x719 `ter00a`: the AMS agents' right hand holding the
// handgun (joint 19 of every player skeleton, the cut-scenes'); 0x717 `tel00a`: Rogan's left hand. The SMG
// has no model in the game: the handgun is drawn then too
constexpr uint32_t kGunHandModel = 0x719, kLeftHandModel = 0x717;

class Scene {
public:
    // after the GL context exists. vrDir: data/vr (vr.ini: the settings)
    bool init(std::unique_ptr<Backend> backend, const std::string& vrDir, std::string& err);
    void shutdown();
    bool active() const { return backend_ != nullptr; }
    Backend* backend() { return backend_.get(); }
    Settings settings;
    // vrcomfort: vr.ini's settings (with --vr-set) as loaded: Options > Gameplay's VR rows (View, Screen Distance /
    // Size, Cut-scenes, Comfort Vignette) put their values over `settings` each frame, their "not set" is these
    // (main.cpp vrComfortApply)
    Settings ini;
    std::vector<std::string> overrides;   // vr3: "key=value" applied over vr.ini at init (main.cpp --vr-set)

    struct Frame {
        game::GameView* view = nullptr;
        const game::Runtime* rt = nullptr;
        game::Hud* hud = nullptr;                    // its sight (Hud::vrSight) for the 3D crosshair
        const game::FrameSnapshot* prev = nullptr;   // Smooth Motion (GameView::draw's)
        float alpha = 1.0f;
        // the 2D HUD into the bound framebuffer (w x h, cleared to transparent; player 1's sight left out: it is in 3D)
        std::function<void(int w, int h)> drawHud;
        float halfW = 248.0f;                        // the game area's half width in arcade units (4:3: 248; 16:9: 341)
        unsigned outFbo = 0;                         // the desktop picture (the window's / --render's framebuffer)
        int outW = 0, outH = 0;
        // vr3: a cut-scene plays (main.cpp: in game, scene_state 4 or the bars on): vr.ini cutscenes decides how
        // (world: around you, no bars on the screen plane; cinema: the flat picture on a big screen)
        bool cutscene = false;
    };
    void frame(const Frame& f);
    // vrattract (user 2026-11-10): player 1 in control, the hands / lasers / 3D crosshairs drawn: in game (mode 4; not
    // the attract demo's mode 3, the title, the menus, the name entry), not the ending, no chapter clear, the shots on
    // (shot_enable: off in the cut-scenes and the chapter cards, as the game's own shot test), player 1 playing
    // (state 5 / 8: the flat crosshair's, Hud::drawSights; not the continue screen)
    static bool playerInControl(const game::Runtime& rt);

    // the gun for the next game tick (from the last frame's controller and hit): the arcade aim, on / off the screen,
    // the trigger pulls since the last take (and a reload from pointing down), held (the SMG's automatic fire)
    struct Aim {
        bool valid = false;
        float x = 0, y = 0;
        bool onScreen = false;
        int pulls = 0;
        bool reload = false;
        // vr4 (dual pistols, a magazine each): which hand's reload it was (0 left: its grip / Y / pointed down / shake;
        // 1 right: its grip / A / B / pointed down / shake); `reload` is either
        bool reloadHand[2] = {false, false};
        bool held = false;
        bool ignore = false;  // vr4: a pull at this aim does nothing (offscreen_reload = below: beside / above / behind)
        bool start = false;   // openxr: the left hand's primary button (X; Index: left A; Vive / WMR: left trackpad)
        // vrflow (user 2026-11-10): a trigger or A / X pressed on either hand, wherever it points (the attract: on to
        // the main menu, as a key or a click in the flat game)
        bool press = false;
        // vrweapon (user 2026-11-11: "a button for Horde / Survival to switch weapons"): the right thumbstick flicked
        // left (-1) / right (+1): the previous / next weapon, as the pad's d-pad (Vive: the trackpad)
        int weaponStep = 0;
        // dual pistols (docs/vr.md "Dual pistols"): the fields above are the right gun's; the left gun's own here
        bool dual = false;
        struct Gun {
            bool valid = false;
            float x = 0, y = 0;
            bool onScreen = false;
            int pulls = 0;
            bool ignore = false;   // (vr4: as Aim::ignore, this gun's)
            bool held = false;     // (its reloads, pointing down / its grip / Y, are `reload`: one magazine for both)
        } left;
    };
    Aim takeAim();
    // dual pistols now (main.cpp each tick: vr.ini dual_pistols, or Survival's Double Pistols in player 1's
    // hands): the left controller holds the mirrored gun hand, aims and fires for itself
    bool dual = false;
    // vrsight (user 2026-11-10): the laser / the 3D crosshair now (main.cpp each frame: Gameplay > Laser (VR) /
    // Crosshair (VR), launcher.ini vrlaser= / vrcrosshair=, else vr.ini laser / crosshair). Off: not drawn for any gun
    // (dual pistols too, the greyed off-screen sight neither); the aim, its depth and the shots stay the same
    bool laserOn = true, crosshairOn = true;
    // a controller's menu button pressed since the last take (main.cpp: the pause)
    bool takeMenu();

    // quest (2026-11-03, docs/quest.md): a flat screen (the setup page, the menus, the end screen, the missing-files
    // message) in VR: the picture already drawn into framebuffer srcFbo (w x h pixels: the window's) is copied onto a
    // big screen settings.menuDistance metres ahead of the start pose (settings.menuWidth wide), the right controller's
    // ray points at it (a laser and a dot). The pointer in the window's coordinates (pointW x pointH: SDL's window
    // size) and the buttons' edges come back: main.cpp turns them into the mouse / keys
    struct Pointer {
        bool valid = false;          // the ray is on the screen
        float x = 0, y = 0;          // window coordinates (top-left origin)
        bool press = false, release = false;   // the trigger's edges (the left mouse button)
        bool back = false;           // B / Y / a menu button (Esc)
        bool enter = false;          // A / X (Enter)
    };
    // (questfb: handsView, the game view whose hand models are drawn in front of the screen; null: no hands)
    Pointer panelFrame(unsigned srcFbo, int w, int h, int pointW, int pointH, game::GameView* handsView = nullptr);
    // the backend's session ended for good (Backend::lost): main.cpp drops VR
    bool lost() const;
    bool exitAsked() const;   // vrflow: lost because the runtime asked the app to quit (Backend::exitAsked)

    // --vr-preview-test: the preview's cursor follows the game's own aim (player 1: --autofire), and each frame checks
    // the whole chain: the world point the game camera sees at that aim (the eye's depth), the gun's ray from its
    // muzzle to it, its hit in the eye's depth, back through the game camera = the same arcade point
    bool followGameAim = false;
    // dual pistols' test: the game's own aim (--autofire's, before the VR guns replace player 1's input) to follow
    bool testAimSet = false;
    float testAim[2] = {0, 0};
    struct TestStats {
        int frames = 0, checked = 0, over = 0, blocked = 0;
        double worst = 0, sum = 0;
        int depthHits = 0;
        int leftChecked = 0, leftDepthHits = 0;   // dual pistols: the left gun's round trips / hits in the depth
        int cinemaFrames = 0;   // vr3: frames on the cinema screen (cutscenes = cinema)
        double leftSum = 0;
        // vr4: the gun's pulls by what they did (offscreen_reload); frames with the game's aim well inside its screen
        // (the gun pointed there) and of those the ones the VR aim did not take as on the screen
        int pullsShot = 0, pullsReload = 0, pullsIgnored = 0;
        int aimOnFrames = 0, aimOffWhileOn = 0;
        // vr4 (hud_distance): frames with a creature inside the HUD panel's view, of those the ones where it was nearer
        // than panel_distance (drawn over by a fixed panel) and nearer than the panel as drawn; frames the panel was
        // pulled in, and its nearest distance (metres)
        // (hudTooNear: the creature nearer than hud_min_distance + 0.25 m, where no panel distance can be in front)
        int hudCreatureFrames = 0, hudOverFixed = 0, hudOverNow = 0, hudNearFrames = 0, hudTooNear = 0;
        float hudNearest = 1e9f;
        int cpuCompared = 0, cpuAgree = 0;   // vr4 (aim_depth = cpu on a desktop): rays the CPU test and the depth agree on
        uint64_t cpuNs = 0;   // (their time)
        int fxMoved = 0;      // vr4: shot flashes / sparks drawn moved out to the hit, their mean distance (metres)
        double fxDistSum = 0;
        int cpuWhy[4] = {};   // (the others: the CPU test missed, the depth missed, the CPU nearer, the CPU farther)
        int fadeFrames = 0;   // vrfade: frames with a full-screen fade over the whole eye (white / cover / PC fade)
        // vrattract: frames with the hands shown, shown outside player 1's control (the attract, cut-scenes, the
        // results...: must be 0), hidden while playing (must be 0), paused (the hands kept: the gun points at the items)
        int handsShown = 0, handsShownOutside = 0, handsHiddenInControl = 0, handsPaused = 0;
        int handsFirstPerson = 0;   // vrfix: shown in a first-person cut-scene (no player body drawn: by design)
        int fireMoved = 0;          // vrfix: the Magician's fireball draws moved out to 1.2 m from the game camera
        int refPitchClamped = 0;   // vrfix: frames the game camera pitched over 60 degrees (the player's space held at 60)
        int leftNearHidden = 0;    // vrfix: (questfb: no longer counted: the left hand near the face is drawn again)
        int panelFrames = 0, panelHandFrames = 0;   // questfb: the big screen's frames, those with the hands drawn
        // bugs7: camera cuts (an instant jump) the comfort vignette ignored; frames it was on over 0.5 (vignette = on)
        int vignetteCuts = 0, vignetteOnFrames = 0;
        // vrcomfort (view = screen): frames of play on the screen, the round trips there (the game's aim -> the
        // cursor on the screen -> the gun's ray meeting the screen -> the aim: the gun points where the last frame's
        // aim was; arcade units)
        int screenPlayFrames = 0, screenChecked = 0, screenOver = 0;
        double screenSum = 0, screenWorst = 0;
    } test;

    // the last frame's state (logs, tests)
    AimResult lastAim, lastAimLeft;   // (lastAimLeft: dual pistols' left gun)
    float lastHitDist = 0;   // game units
    float vignetteNow = 0;
    float panelNow = 0;   // vr4: the HUD panel's distance as last drawn (metres; hud_distance = adaptive moves it)
    bool cinemaNow = false;   // vr3: the last frame showed the cinema screen (cutscenes = cinema in a cut-scene)
    // vrflow (user 2026-11-10, "a floating white Quest controller": the runtime's own room shows when the game submits
    // no frame): what the game shows (main.cpp sets it: "menu", "game", "attract", "pause", "loading" ...); a gap of
    // more than 50 ms between two frames is logged "vr: frame gap N ms in <where>" (frameGaps / worstGapMs: tests)
    std::string where = "start";
    int frameGaps = 0;
    double worstGapMs = 0;
    std::string worstGapWhere;

private:
    std::unique_ptr<Backend> backend_;
    FrameState fs_;
    std::string vrDir_;
    // GL
    unsigned spriteProg_ = 0;
    int uVp_ = -1, uTex_ = -1, uMode_ = -1, uTint_ = -1;
    unsigned spriteVao_ = 0, spriteVbo_ = 0, whiteTex_ = 0;
    unsigned panelFbo_ = 0, panelTex_ = 0;
    int panelW_ = 0, panelH_ = 0;
#if defined(__ANDROID__)
    // quest (2026-11-10): OpenGL ES never reads depth: the CPU ray from the first frame (no aim pass, no failed
    // ReadPixels; an Adreno driver that accepted it could hand back junk)
    bool depthReadable_ = false;
#else
    bool depthReadable_ = true;
#endif
    std::vector<float> depthBuf_;
    // aim / input
    Aim pending_;
    bool triggerDown_ = false, wasDown_ = false, wasLow_ = false;
    bool triggerDownL_ = false, wasDownL_ = false, wasLowL_ = false;   // dual pistols: the left gun's
    ReloadGestures gestures_[2];   // shake / wrist-flick reload: one per hand (a gun each with dual pistols)
    bool gripDown_[2] = {}, wasReload_[2] = {}, wasMenu_ = false, menuPending_ = false, wasStart_ = false;
    int controlRun_ = 0;   // vrattract: frames in a row with player 1 in control (up to 2: the hands shown)
    // comfort
    float prevFwd_[3] = {0, 0, 0};
    float prevPos_[3] = {0, 0, 0};   // (bugs7: the reference's place, a camera cut's jump)
    int64_t prevTime_ = 0;
    HudDistance hudDist_;     // vr4: the HUD panel's adaptive distance
    std::vector<DrawItem> fxItems_;     // vr4: the frame's items with the shot flash / spark moved to the hit
    std::vector<Placement> fxPlaces_;   // (their moved placements)
    int64_t hudTime_ = 0;
    // quest: the flat screens' big screen (panelFrame)
    unsigned screenFbo_ = 0, screenTex_ = 0;
    int screenW_ = 0, screenH_ = 0;
    bool panelTrigger_ = false, panelBack_ = false, panelEnter_ = false;
    // vrflow: the last frame was the big screen's (panelFrame) / the game's (frame): at the switch the buttons held
    // are taken as held (the trigger that left the attract never clicks a menu item; the click on Arcade never shoots)
    bool lastPanel_ = false;
    bool pressHeld_ = false;   // (Aim::press's edge)
    bool stickHeld_ = false;   // (vrweapon: Aim::weaponStep's edge: the stick back near the middle first)
    uint64_t lastBeginNs_ = 0;   // (where's frame gaps)
    void noteFrame();
    // vr3: the cinema screen's picture (drawCinema)
    unsigned cinemaFbo_ = 0, cinemaTex_ = 0, cinemaDepth_ = 0;
    int cinemaW_ = 0, cinemaH_ = 0;
    void drawCinema(game::GameView& view, const std::vector<DrawItem>& items, const Camera& cam, float halfW);
    // vrfix (user's PC VR playthrough 2026-11-10: the name entry was a black void, only the hands): the name entry on
    // the cinema screen, aimed through it: the gun's ray where it meets the screen (seat space, metres: o -> p), that
    // point as the arcade's aim (x, y; hit: the ray meets the screen), the hands in seat space (game units)
    // vrcomfort (view = screen): all of the game on the cinema screen, aimed the same way: the right gun (screenAim_)
    // and the left one with dual pistols (screenAimL_); down: the gun pointed steeply down (the player's own down:
    // the seat's); sight: the sights drawn on the picture (in control / paused, the name entry)
    struct ScreenAim {
        bool on = false, hit = false, down = false;
        float x = 0, y = 0, o[3] = {0, 0, 0}, p[3] = {0, 0, 0};
    } screenAim_, screenAimL_;
    bool screenSight_ = false;
    float screenTarget_[2] = {0, 0};   // (--vr-preview-test: the aim the cursor was put on last frame)
    bool screenTargetSet_ = false;
    std::vector<DrawItem> cinemaHands_;
    float refYaw_ = 0.0f;      // vrfix: the player's space's last heading (held while the game camera is near vertical)
    bool refYawSet_ = false;
    bool leftNear_ = false;    // vrfix: the empty left hand too near the head (not drawn)
    int firstPersonRun_ = 0;   // vrfix: frames running of an in-game world cut-scene with no player body drawn
    bool firstPersonLast_ = false;   // (the hands shown by a first-person cut-scene last frame: no blink into control)
    bool handsShownPrev_ = false;    // (--vr-preview-test: the hands shown last frame: a blink is counted)
    std::vector<DrawItem> nearLayer_;   // vrfix: the hands and the extra-life item over the world (the HUD's depth test)
    const Frame* frame_ = nullptr;   // (the frame being drawn: drawCinema's sight)
    // vrnull: the flat picture + HUD into cinemaFbo_ (the cinema's screen; desktop_view spectator); flatFrame_: drawn
    // this frame
    void flatPicture(game::GameView& view, const std::vector<DrawItem>& items, const Camera& cam, float halfW);
    bool flatFrame_ = false;
    // vrnull: the aim's own depth of the right eye (the free camera's narrow key band: the eyes' 0.2 band pulls their
    // depth toward the sort keys; the gun's rays read true surfaces here)
    unsigned aimFbo_ = 0, aimTex_ = 0, aimDepthRb_ = 0;
    int aimW_ = 0, aimH_ = 0;
    void presentSpectator(const Frame& f);
    // vrfade: this frame's full-screen fades (game::Hud::screenFade: the white fade's alpha, the cover 0 / 1 black /
    // 2 white, the PC fade's level), drawn over the whole of each eye (and the cinema / spectator picture): under the
    // 2D (the white fade, the cover) or over it (the PC fade). The bound framebuffer's whole viewport
    float fadeWhite_ = 0.0f, fadePc_ = 1.0f;
    int fadeCover_ = 0;
    void drawFades(bool under);

    bool makePrograms(std::string& err);
    void ensurePanel(int w, int h);
    // how far a world ray goes before the bound eye framebuffer's depth (vp: its view-projection); -1: nothing hit
    float march(const float o[3], const float d[3], const float vp[16], int w, int h, float maxDist);
    // vr4: how far a world ray goes by the CPU (the stage's triangles, the creatures' parts); -1: nothing hit
    float cpuRay(const game::GameView& view, const std::vector<DrawItem>& items, const float o[3], const float d[3], float maxDist);
    void drawSprite(const float* verts, int count, unsigned tex, int mode, const float tint[4], const float vp[16]);
};

} // namespace hotd::vr
