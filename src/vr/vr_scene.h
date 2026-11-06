// VR mode: the VR frame (docs/vr.md). Per frame: the backend's poses; the 2D HUD into the screen-plane panel; the
// game's 3D items drawn once per eye (GameView::drawOverride: the game builds its list once, the renderer draws it
// from each eye: Renderer::eye); over the world the player's hands (the game's own models on the controllers), the laser,
// the game's own crosshair sprite at the ray's hit, the panel, the comfort vignette; then the backend's hand-over and
// the desktop picture. The gun's aim for the next game tick comes out of it (takeAim).
#pragma once

#include "vr/vr.h"
#include "vr/vr_aim.h"
#include "vr/vr_math.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

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
    };
    void frame(const Frame& f);

    // the gun for the next game tick (from the last frame's controller and hit): the arcade aim, on / off the screen,
    // the trigger pulls since the last take (and a reload from pointing down), held (the SMG's automatic fire)
    struct Aim {
        bool valid = false;
        float x = 0, y = 0;
        bool onScreen = false;
        int pulls = 0;
        bool reload = false;
        bool held = false;
        bool start = false;   // openxr: the left hand's primary button (X; Index: left A; Vive / WMR: left trackpad)
        // dual pistols (docs/vr.md "Dual pistols"): the fields above are the right gun's; the left gun's own here
        bool dual = false;
        struct Gun {
            bool valid = false;
            float x = 0, y = 0;
            bool onScreen = false;
            int pulls = 0;
            bool held = false;     // (its reloads, pointing down / its grip / Y, are `reload`: one magazine for both)
        } left;
    };
    Aim takeAim();
    // dual pistols now (main.cpp each tick: vr.ini dual_pistols, or Survival's Double Pistols in player 1's
    // hands): the left controller holds the mirrored gun hand, aims and fires for itself
    bool dual = false;
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
    Pointer panelFrame(unsigned srcFbo, int w, int h, int pointW, int pointH);
    // the backend's session ended for good (Backend::lost): main.cpp drops VR
    bool lost() const;

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
        double leftSum = 0;
    } test;

    // the last frame's state (logs, tests)
    AimResult lastAim, lastAimLeft;   // (lastAimLeft: dual pistols' left gun)
    float lastHitDist = 0;   // game units
    float vignetteNow = 0;

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
    bool depthReadable_ = true;
    std::vector<float> depthBuf_;
    // aim / input
    Aim pending_;
    bool triggerDown_ = false, wasDown_ = false, wasLow_ = false;
    bool triggerDownL_ = false, wasDownL_ = false, wasLowL_ = false;   // dual pistols: the left gun's
    bool gripDown_[2] = {}, wasReload_[2] = {}, wasMenu_ = false, menuPending_ = false, wasStart_ = false;
    // comfort
    float prevFwd_[3] = {0, 0, 0};
    int64_t prevTime_ = 0;
    // quest: the flat screens' big screen (panelFrame)
    unsigned screenFbo_ = 0, screenTex_ = 0;
    int screenW_ = 0, screenH_ = 0;
    bool panelTrigger_ = false, panelBack_ = false, panelEnter_ = false;

    bool makePrograms(std::string& err);
    void ensurePanel(int w, int h);
    // how far a world ray goes before the bound eye framebuffer's depth (vp: its view-projection); -1: nothing hit
    float march(const float o[3], const float d[3], const float vp[16], int w, int h, float maxDist);
    void drawSprite(const float* verts, int count, unsigned tex, int mode, const float tint[4], const float vp[16]);
};

} // namespace hotd::vr
