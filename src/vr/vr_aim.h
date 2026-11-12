// VR mode: the gun's ray -> the arcade's 2D aim (docs/vr.md "Aiming"). The arcade is a light-gun game: the player
// shoots at a point of its 496 x 384 screen (centre 0, x right, y up, +-248 x +-192; widescreen: wider), and the game
// tests what the game camera sees there. In VR the gun is a ray in the 3D world, so the point it hits (the eye's depth
// tells how far the ray goes) is projected through the game camera (eye-independent: the player's head does not
// move the aim) to the arcade screen. Off that screen, pointing away or steeply down = off screen (a shot reloads, as
// the light gun's off-screen shot). Pure math: no GL (tested by --vr-preview-test).
#pragma once

#include "vr/vr_math.h"

#include <algorithm>
#include <cstdint>

namespace hotd::vr {

// the game camera of the newest frame: camera -> world (left-handed, game units; columns right / up / forward) and
// its focal length (Runtime::focal: 496 unless the game zooms)
struct GameCamera {
    Rigid toWorld;
    float focal = 496.0f;
    float halfW = 248.0f;    // the playable half width in arcade units (4:3: 248; widescreen: 192 x the panel's aspect)
    float halfH = 192.0f;
};

struct AimResult {
    float x = 0, y = 0;      // arcade screen units
    bool onScreen = false;
    bool down = false;       // the gun points steeply down (reload gesture)
    bool behind = false;     // the hit lies behind the game camera (pointing away)
    float hit[3] = {0, 0, 0};   // the hit point (world)
};

// origin / dir: the gun's ray in the world (dir need not be unit); dist: how far it goes (game units); worldUp: the
// world's up (+y); reloadPitchDeg: below this angle under the horizon the gun points "down"
AimResult aimFromRay(const GameCamera& cam, const float origin[3], const float dir[3], float dist, const float worldUp[3],
                     float reloadPitchDeg);

// vr4 (2026-11-09, docs/vr.md "Off the screen"): what a trigger pull at this aim does. On the screen: a shot. Off it,
// policy 1 (vr.ini offscreen_reload = any): a reload, the light gun's rule. Policy 0 (below, the default): a reload
// only when the gun points down (reload_down's angle) or the aim lies below the screen (in front of the game camera,
// under the bottom edge, inside a wedge that widens downward from the screen's sides); beside / above / behind the
// screen the pull does nothing (Ignore): the world is drawn all around in VR, and a creature beside you is not one the
// game can hit
enum class Pull { Shoot, Reload, Ignore };
Pull pullAction(const AimResult& a, float halfW, float halfH, int policy);

// the inverse (tests, the preview's mouse): the world point at distance `dist` along the game camera's ray through
// arcade point (x, y)
void arcadeToWorld(const GameCamera& cam, float x, float y, float dist, float out[3]);

// the game camera from the renderer's Camera parameters (position, yaw, pitch: GameView::cam's convention, no roll)
GameCamera gameCameraFrom(const float pos[3], float yaw, float pitch, float focal);

// shake-to-reload (user 2026-11-09; docs/vr.md "Shake to reload"): one hand's controller pose per frame in, true when a
// shake happened. vrgest (user 2026-11-10 on a Quest 3: "I expected the same ease as a DualSense controller"): the
// DualSense's rule (game/shake.h: one jerk of the pad's acceleration, any axis) where VR allows it. Two ways, either
// reloads, each from the poses' own time stamps (dropped frames, uneven times: the derivatives use the real gaps):
// - a jolt: the grip's acceleration (the second derivative of its position, smoothed over ~12 ms) at / over `g`
//   (vr.ini shake_sensitivity, in g, default 2), then within 0.25 s (and 35 ms or more later) at / over half of it the
//   opposite way: the hand's start and its stop. One brisk move in any direction is enough (a shake has many). Aiming
//   is told apart by the turn: over the jolt the gun's aim may turn at most 1 / 0.7 m x the grip's travel in radians
//   (8 degrees for 10 cm; an arm sweeping the gun round the shoulder turns more than that) and the grip must travel
//   3 cm or more (tracking noise, a jump there and back);
// - a twist (the wrist, the grip in place): the gun's angular velocity at / over 350 deg/s x g / 3 (233 at 2 g) swept
//   20 degrees or more, then within 0.15 s at / over 3 / 4 of that the other way for 15 degrees: a back-and-forth
//   turn. A single turn is aiming (a flick up has its own rule: FlickDetector); fast sweeps across targets pause before turning back,
//   a trembling hand or a recoil kick never sweep 20 degrees.
// A speed over 6 m/s or a turn over 2500 deg/s in one frame is a tracking jump (the history starts again), as are an
// untracked pose and a gap over 0.1 s. After a shake, 0.7 s of quiet (one shake = one reload).
class ShakeDetector {
public:
    bool update(const Pose& grip, int64_t timeNs, float g);
    void reset() { have_ = false; nv_ = 0; jolt_ = false; twist_ = 0; }
    // vrflick: no shake until `untilSec` (the pose's time in seconds: ReloadGestures' shared cooldown)
    void quiet(double untilSec) { jolt_ = false; twist_ = 0; quietUntil_ = std::max(quietUntil_, untilSec); }
    const char* kind() const { return kind_; }   // the last shake's way: "jolt" / "twist" (the log)
private:
    const char* kind_ = "";
    bool have_ = false, jolt_ = false;
    int nv_ = 0;                                  // velocities known (an acceleration needs 2)
    float pos_[3] = {0, 0, 0}, rot_[4] = {0, 0, 0, 1}, vel_[3] = {0, 0, 0}, acc_[3] = {0, 0, 0};
    double t_ = 0, velT_ = 0;                     // the last pose's time; the last velocity's (mid-frame) time
    float joltDir_[3] = {0, 0, 0}, joltPos_[3] = {0, 0, 0}, joltRot_[4] = {0, 0, 0, 1};
    double joltT_ = 0;
    int joltRun_ = 0;                             // frames since the start moving its way at 0.4 m/s or more (needs 2)
    int twist_ = 0;                              // 1: the first swing turning; 2: it swept enough, the way back awaited
    float axis_[3] = {0, 0, 0}, swept_ = 0, back_ = 0;   // the first swing's axis; degrees swept / back
    int swingN_ = 0, backN_ = 0;                  // (their frames: 2 or more each way)
    float sweptMax_ = 0, backMax_ = 0;            // (their largest frame: under 65 % of the way)
    double twistT_ = 0;                           // the first swing's end
    double quietUntil_ = 0;
};

// wrist-flick reload (user 2026-11-10; docs/vr.md "Wrist flick to reload"): the Wii light-gun games' flick-up reload.
// One hand's controller pose per frame in, true on a flick: a single sharp upward snap of the muzzle, then a stop.
// - the gun's pitch rate (its angular velocity about its own lateral x axis, muzzle up; from the frame-to-frame
//   rotation and the poses' time stamps) is at / over `degPerSec` (vr.ini flick_sensitivity, default 300; vrgest: was
//   600) on 2 frames in a row or more (frames, not time: one long frame over a dropped one may be a tracking jump),
//   with the pitch rate at least twice the yaw rate (a fast sweep across targets is yaw-dominant);
// - within the last 0.2 s the muzzle's elevation rose by 20 degrees or more (vrgest: was 30 in 0.15 s), and its
//   sideways turn over the same stretch is at most half that rise (fast diagonal aim moves between targets; downward /
//   sideways snaps never rise);
// - then within 0.35 s it stops or comes back (the pitch rate under a quarter of `degPerSec`): the reload is then;
//   still turning up after 0.35 s: none (a spin, a long sweep).
// A frame turning over 2500 deg/s is a tracking jump, not a motion (dropped, as an untracked pose and a gap over
// 0.1 s); after a flick 0.7 s of quiet. A flick of the wrist and one of the forearm both turn the gun.
class FlickDetector {
public:
    bool update(const Pose& grip, int64_t timeNs, float degPerSec);
    void reset() { n_ = 0; fastRun_ = 0; armed_ = false; }
    void quiet(double untilSec) { armed_ = false; fastRun_ = 0; quietUntil_ = std::max(quietUntil_, untilSec); }
private:
    static constexpr int kHist = 64;          // the last frames' muzzle directions (0.15 s = 22 frames at 144 Hz)
    float dir_[kHist][3] = {};
    double time_[kHist] = {};
    int n_ = 0, head_ = 0;                    // (entries kept; the newest)
    float rot_[4] = {0, 0, 0, 1};             // the newest entry's grip rotation
    int fastRun_ = 0;                         // frames in a row at / over the rate
    double fastT_ = -1, armT_ = 0, quietUntil_ = 0;
    bool armed_ = false;
};

// vrflick: one gun's reload gestures (a shake, a flick) with a shared 0.7 s cooldown: one gesture = one reload, never
// two (a hard flick that comes back can also pass the shake's speed both ways)
class ReloadGestures {
public:
    bool update(const Pose& grip, int64_t timeNs, bool shakeOn, float shakeMps, bool flickOn, float flickDps);
    void reset() { shake_.reset(); flick_.reset(); }
    const char* last() const { return last_; }   // the last reload's gesture: "shake (jolt)" / "shake (twist)" / "flick"
private:
    const char* last_ = "";
    ShakeDetector shake_;
    FlickDetector flick_;
    double quietUntil_ = 0;
};

// vr4 (docs/vr.md "Aim depth", "The HUD and close creatures"): ray tests on the CPU (no depth buffer needed: OpenGL ES
// cannot read it back). A box placed in the world (world = rot x local + pos, rot row-major and possibly scaled, the
// game's DrawItem placement): the ray's first entry past minT (t in the ray's own parameter: |dir| x t); false: missed
// or only behind minT. Triangles: 12 floats each (3 corners, a normal: game/ragdoll.h RagdollWalls), both sides, the
// nearest t in (minT, best) put in best
bool rayBox(const float o[3], const float d[3], const float lo[3], const float hi[3], float& t0, float& t1);
bool rayPlacedBox(const float o[3], const float d[3], const float rot[9], const float pos[3], const float lo[3],
                  const float hi[3], float minT, float maxT, float& t, float* tExit = nullptr);
bool rayTriangles(const float o[3], const float d[3], const float* tris, size_t count, float minT, float& best);
// questsim (2026-11-10): a placed model's own triangles (a triangle list of vertices `stride` bytes apart, each starting
// with its model-space position x, y, z: assets.h Vertex) as rayPlacedBox places them; the nearest t in (minT, best)
// put in best
bool rayPlacedTriangles(const float o[3], const float d[3], const float rot[9], const float pos[3], const void* verts,
                        size_t stride, size_t vertexCount, float minT, float& best);

// vr4: the HUD panel's distance (metres) easing toward `target` (adaptive: just in front of the nearest creature,
// panel_distance when none): nearer quickly (a lunge: ~0.1 s), back out slowly (~0.7 s) after 0.5 s with nothing nearer
// (no pumping as a creature's parts move)
struct HudDistance {
    float now = -1.0f;   // (< 0: not started: jumps to the first target)
    float hold = 0.0f;   // seconds left before easing back out
    float update(float target, float dt);
};

// vr4 (docs/quest.md "Refresh rate"): the display rate to ask for among the runtime's (XR_FB_display_refresh_rate).
// want 0 (auto): 90 on a standalone headset (or the highest offered under it), 0 elsewhere (a PC runtime's own choice:
// Quest Link / SteamVR set it in their own settings); 72 / 90 / 120 ...: the offered rate nearest to it. 0: ask nothing
float chooseRefreshRate(const float* rates, int count, int want, bool standalone);

// vr4: the recentre's new play space (in the runtime's LOCAL space): the head's place and heading (yaw only: the
// horizon stays level). The head's position not valid (a 3DoF moment, no boundary): the heading alone, the place kept
// (`keep`: the play space's position now). False: the orientation is not valid either (nothing to do)
bool recentrePose(const Pose& head, bool orientationValid, bool positionValid, const float keep[3], Pose& out);

// vr4: the headset away (taken off, the app hidden: not shown) and back: true once, on its return, when it was away
// longer than `seconds` (0: never) (docs/vr.md "Recentre": the seat recentred by itself)
class AwayClock {
public:
    bool update(bool shown, double now, double seconds);
private:
    bool away_ = false, started_ = false;
    double since_ = 0;
};

// --vr-preview-test's math part: known rays -> known arcade points; prints each failure, returns the failures
int selfTest(int& checks);

} // namespace hotd::vr
