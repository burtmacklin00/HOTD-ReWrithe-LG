// VR mode: the gun's ray -> the arcade's 2D aim (docs/vr.md "Aiming"). The arcade is a light-gun game: the player
// shoots at a point of its 496 x 384 screen (centre 0, x right, y up, +-248 x +-192; widescreen: wider), and the game
// tests what the game camera sees there. In VR the gun is a ray in the 3D world, so the point it hits (the eye's depth
// tells how far the ray goes) is projected through the game camera (eye-independent: the player's head does not
// move the aim) to the arcade screen. Off that screen, pointing away or steeply down = off screen (a shot reloads, as
// the light gun's off-screen shot). Pure math: no GL (tested by --vr-preview-test).
#pragma once

#include "vr/vr_math.h"

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

// the inverse (tests, the preview's mouse): the world point at distance `dist` along the game camera's ray through
// arcade point (x, y)
void arcadeToWorld(const GameCamera& cam, float x, float y, float dist, float out[3]);

// the game camera from the renderer's Camera parameters (position, yaw, pitch: GameView::cam's convention, no roll)
GameCamera gameCameraFrom(const float pos[3], float yaw, float pitch, float focal);

// --vr-preview-test's math part: known rays -> known arcade points; prints each failure, returns the failures
int selfTest(int& checks);

} // namespace hotd::vr
